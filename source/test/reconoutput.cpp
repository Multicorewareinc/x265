#include "x265.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct ReconPicture
{
    std::vector<unsigned char> pixels;
    int type = -1;
    int bitDepth = 0;
    int colorSpace = 0;
    int64_t pts = 0;
    int64_t dts = 0;
};

struct EncodedOutput
{
    std::vector<unsigned char> stream;
    std::vector<ReconPicture> pictures;
};

struct TestConfig
{
    const char *path;
    int width, height, fps, frames;
    int threads, slices, wpp;
    const char *preset;
    int optionCount;
    char **options;
};

struct EncoderContext
{
    FILE *input = NULL;
    x265_param *param = x265_param_alloc();
    x265_encoder *encoder = NULL;

    EncoderContext() = default;
    EncoderContext(const EncoderContext &) = delete;
    EncoderContext &operator=(const EncoderContext &) = delete;

    ~EncoderContext()
    {
        if (encoder)
            x265_encoder_close(encoder);
        if (param)
            x265_param_free(param);
        if (input)
            fclose(input);
    }
};

enum OutputMode
{
    OUTPUT_ALWAYS,
    OUTPUT_NEVER,
    OUTPUT_ALTERNATE,
    OUTPUT_FLUSH,
    OUTPUT_LATE
};

static bool encode(const TestConfig &config, OutputMode mode, int sao, int guard, EncodedOutput &result)
{
    EncoderContext context;
    context.input = fopen(config.path, "rb");
    x265_param *param = context.param;
    if (!context.input || !param || x265_param_default_preset(param, config.preset, NULL))
        return false;

    param->bframes = 7;
    param->bFrameAdaptive = 0;
    param->bBPyramid = 2;
    param->lookaheadDepth = 16;
    param->lookaheadSlices = 0;
    param->frameNumThreads = config.threads;
    param->maxSlices = config.slices;
    param->bEnableWavefront = config.wpp;
    param->scenecutThreshold = 0;
    if (x265_param_parse(param, "pools", "4"))
        return false;
    for (int i = 0; i < config.optionCount; i++)
    {
        const char *option = config.options[i];
        const char *value = strchr(option, '=');
        std::string name(option, value - option);
        if (name != "preset" && x265_param_parse(param, name.c_str(), value + 1))
        {
            fprintf(stderr, "Invalid encoder option: %s\n", option);
            return false;
        }
    }
    param->sourceWidth = config.width;
    param->sourceHeight = config.height;
    param->fpsNum = config.fps;
    param->fpsDenom = 1;
    param->totalFrames = config.frames;
    param->internalCsp = X265_CSP_I420;
    param->bEnableSAO = sao;
    param->decodedPictureHashSEI = guard < 4 ? guard : 0;
    param->bEnablePsnr = guard == 4;
    param->bEnableSsim = guard == 4;
    param->bEmitInfoSEI = 0;
    param->bRepeatHeaders = 1;
    param->logLevel = X265_LOG_ERROR;
    context.encoder = x265_encoder_open(param);
    if (!context.encoder)
        return false;

    x265_picture input, output;
    x265_picture_init(param, &input);
    input.bitDepth = 8;
    std::vector<unsigned char> pixels((size_t)config.width * config.height * 3 / 2);
    input.planes[0] = pixels.data();
    input.planes[1] = pixels.data() + (size_t)config.width * config.height;
    input.planes[2] = pixels.data() + (size_t)config.width * config.height * 5 / 4;
    input.stride[0] = config.width;
    input.stride[1] = input.stride[2] = config.width / 2;
    result.pictures.resize(config.frames);
    int outputCount = 0;
    bool flushed = false;

    for (int call = 0; call < config.frames * 2 + 128; call++)
    {
        if (call < config.frames && fread(pixels.data(), 1, pixels.size(), context.input) != pixels.size())
            return false;
        input.pts = call;
        x265_picture_init(param, &output);
        bool wantOutput = mode == OUTPUT_ALWAYS || (mode == OUTPUT_ALTERNATE && call % 2) ||
                          (mode == OUTPUT_FLUSH && call >= config.frames) ||
                          (mode == OUTPUT_LATE && outputCount >= config.frames / 2);
        x265_nal *nals = NULL;
        uint32_t count = 0;
        int encoded = x265_encoder_encode(context.encoder, &nals, &count, call < config.frames ? &input : NULL,
                                          wantOutput ? &output : NULL);
        if (encoded < 0)
            return false;
        for (uint32_t i = 0; i < count; i++)
            result.stream.insert(result.stream.end(), nals[i].payload, nals[i].payload + nals[i].sizeBytes);
        outputCount += encoded;
        if (encoded && wantOutput)
        {
            if (output.poc < 0 || output.poc >= config.frames || output.pts != output.poc)
                return false;
            ReconPicture &picture = result.pictures[output.poc];
            if (!picture.pixels.empty())
                return false;
            picture.type = output.sliceType;
            picture.bitDepth = output.bitDepth;
            picture.colorSpace = output.colorSpace;
            picture.pts = output.pts;
            picture.dts = output.dts;
            for (int plane = 0; plane < 3; plane++)
            {
                if (!output.planes[plane] || output.stride[plane] <= 0)
                    return false;
                int width = plane ? config.width / 2 : config.width;
                int height = plane ? config.height / 2 : config.height;
                int rowBytes = width * (output.bitDepth > 8 ? 2 : 1);
                const unsigned char *row = static_cast<const unsigned char *>(output.planes[plane]);
                for (int y = 0; y < height; y++, row += output.stride[plane])
                    picture.pixels.insert(picture.pixels.end(), row, row + rowBytes);
            }
        }
        if (!encoded && call >= config.frames)
        {
            flushed = true;
            break;
        }
    }
    return flushed && outputCount == config.frames;
}

static bool compare(const EncodedOutput &expected, const EncodedOutput &actual, OutputMode mode, int &nonReferenceB)
{
    if (expected.stream != actual.stream || expected.pictures.size() != actual.pictures.size())
    {
        fprintf(stderr, "Bitstream differs\n");
        return false;
    }
    int returned = 0;
    for (size_t i = 0; i < actual.pictures.size(); i++)
    {
        const ReconPicture &a = expected.pictures[i];
        const ReconPicture &b = actual.pictures[i];
        if (b.pixels.empty())
            continue;
        returned++;
        nonReferenceB += b.type == X265_TYPE_B;
        if (a.pixels != b.pixels || a.type != b.type || a.bitDepth != b.bitDepth || a.colorSpace != b.colorSpace ||
            a.pts != b.pts || a.dts != b.dts)
        {
            fprintf(stderr, "Reconstructed picture or metadata differs: POC=%zu\n", i);
            return false;
        }
    }
    return mode != OUTPUT_NEVER || returned == 0;
}

int main(int argc, char **argv)
{
    if (argc < 6)
    {
        fprintf(stderr, "Usage: ReconOutputTest input.yuv width height fps frames [threads slices wpp] "
                        "[preset=name] [name=value ...]\n");
        return 2;
    }
    TestConfig config = {argv[1], atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), 1, 1, 1, "fast", 0, NULL};
    int option = 6;
    int *threadOptions[] = {&config.threads, &config.slices, &config.wpp};
    for (int i = 0; i < 3 && option < argc && !strchr(argv[option], '='); i++, option++)
        *threadOptions[i] = atoi(argv[option]);
    config.optionCount = argc - option;
    config.options = argv + option;
    for (; option < argc; option++)
    {
        const char *value = strchr(argv[option], '=');
        if (!value || value == argv[option] || !value[1])
            return 2;
        if (!strncmp(argv[option], "preset=", 7))
            config.preset = value + 1;
    }
    if (config.width <= 0 || config.height <= 0 || config.width % 2 || config.height % 2 || config.fps <= 0 ||
        config.frames < 1 || config.threads < 1 || config.slices < 1 || config.wpp < 0 || config.wpp > 1)
        return 2;

    int comparisons = 0;
    int nonReferenceB = 0;
    bool passed = true;
    for (int sao = 0; sao <= 1 && passed; sao++)
        for (int guard = 0; guard <= 4 && passed; guard++)
        {
            EncodedOutput expected;
            passed = encode(config, OUTPUT_ALWAYS, sao, guard, expected);
            for (int mode = OUTPUT_NEVER; mode <= OUTPUT_LATE && passed; mode++)
            {
                EncodedOutput actual;
                passed = encode(config, (OutputMode)mode, sao, guard, actual) &&
                         compare(expected, actual, (OutputMode)mode, nonReferenceB);
                if (!passed)
                    fprintf(stderr, "Output differs: SAO=%d guard=%d output-mode=%d\n", sao, guard, mode);
                comparisons++;
            }
        }
    x265_cleanup();
    if (!passed)
        return 1;
    printf("Bitstreams, reconstructed pixels and metadata match in %d output comparisons.\n", comparisons);
    printf("Compared %d non-reference B reconstructions.\n", nonReferenceB);
    return 0;
}
