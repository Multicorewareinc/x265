/*****************************************************************************
 * Copyright (C) 2026 MulticoreWare, Inc
 *
 * Authors: Dash Santosh <dash.sathyanarayanan@multicorewareinc.com>
 *          Priyadarsini Shanmugam <priyadarsini.shanmugam@multicorewareinc.com>
 *          Sangeetha Ravichandran <sangeetha.ravichandran@multicorewareinc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *****************************************************************************/

#ifdef ENABLE_MLCTUPRED

#include "common.h"
#include "primitives.h"
#include "mlctu.h"
#include "mlutil.h"

#include <cstring>
#include <cstdlib>

/* MSVC OpenMP 2.0 has no simd; it auto-vectorizes these loops */
#if defined(_OPENMP) && !defined(_MSC_VER)
#define ML_OMP_SIMD _Pragma("omp simd")
#else
#define ML_OMP_SIMD
#endif

namespace X265_NS {
// private x265 namespace

/* One model per QP range, see mlSessionForQP() */
static const char* const s_modelFiles[NUM_MODELS] =
{
    "eth_cnn_qp1-15.onnx",
    "eth_cnn_qp16-35.onnx",
    "eth_cnn_qp36-51.onnx"
};

static int mlSessionForQP(int qp)
{
    if (qp <= 15)
        return 0;
    if (qp <= 35)
        return 1;
    return 2;
}

/* Raster to Z-scan order of the 16x16 blocks in a 64x64 block */
static const uint8_t s_rasterToZ16[16] =
{
     0,  1,  4,  5,
     2,  3,  6,  7,
     8,  9, 12, 13,
    10, 11, 14, 15
};

static bool modelsExistIn(const char* dir)
{
    char path[ML_MAX_PATH];
    for (int i = 0; i < NUM_MODELS; i++)
    {
        if (!joinPath(path, sizeof(path), dir, s_modelFiles[i]))
            return false;
        FILE* f = x265_fopen(path, "rb");
        if (!f)
            return false;
        fclose(f);
    }
    return true;
}

/* CPUs split evenly across concurrent inferences, 1 to ML_MAX_INTRAOP_THREADS */
static int mlIntraOpThreads(int poolWidth, int cpuCount)
{
    if (poolWidth < 1)
        poolWidth = 1;
    return x265_clip3(1, ML_MAX_INTRAOP_THREADS, cpuCount / poolWidth);
}

MLCTUPredictor::MLCTUPredictor(x265_param* param)
    : m_param(param)
    , m_numBlocksW((param->sourceWidth + ML_BLOCK_SIZE - 1) / ML_BLOCK_SIZE)
    , m_numBlocksH((param->sourceHeight + ML_BLOCK_SIZE - 1) / ML_BLOCK_SIZE)
    , m_numBlocks(m_numBlocksW * m_numBlocksH)
    , m_intraOpThreads(ML_MAX_INTRAOP_THREADS)
    , m_prepThreads(ML_MAX_INTRAOP_THREADS)
    , m_ort(NULL)
    , m_env(NULL)
    , m_memoryInfo(NULL)
{
    for (int i = 0; i < NUM_MODELS; i++)
        m_sessions[i] = NULL;
}

MLCTUPredictor::~MLCTUPredictor()
{
    release();
}

bool MLCTUPredictor::checkStatus(OrtStatus* status, const char* what, int level) const
{
    if (!status)
        return true;
    x265_log(m_param, level, "ML CTU pred: failed to %s: %s\n", what, m_ort->GetErrorMessage(status));
    m_ort->ReleaseStatus(status);
    return false;
}

/* First directory holding all models:
 *   1. --ml-model-dir, else X265_ML_MODEL_DIR; no fallback if either is set
 *   2. <binary dir>/models
 *   3. install dir relative to the binary, for relocated installs
 *   4. absolute install dir */
bool MLCTUPredictor::resolveModelDir(char* dir, size_t size) const
{
    const bool bUserDir = m_param->mlModelDir[0]
                        ? snprintf(dir, size, "%s", m_param->mlModelDir) > 0
                        : getEnvPath("X265_ML_MODEL_DIR", dir, size);
    if (bUserDir)
    {
        if (modelsExistIn(dir))
            return true;
        x265_log(m_param, X265_LOG_ERROR, "ML CTU pred: model files (%s, ...) not found in %s\n", s_modelFiles[0], dir);
        return false;
    }

    static const char* const relDirs[] =
    {
        "models",
#ifdef X265_ML_MODEL_REL_BIN_DIR
        X265_ML_MODEL_REL_BIN_DIR,
#endif
#ifdef X265_ML_MODEL_REL_LIB_DIR
        X265_ML_MODEL_REL_LIB_DIR,
#endif
    };
    char moduleDir[ML_MAX_PATH];
    if (getModuleDir(moduleDir, sizeof(moduleDir)))
    {
        for (size_t i = 0; i < sizeof(relDirs) / sizeof(relDirs[0]); i++)
            if (relDirs[i][0] && joinPath(dir, size, moduleDir, relDirs[i]) && modelsExistIn(dir))
                return true;
    }

#ifdef X265_ML_MODEL_INSTALL_DIR
    const char* installDir = X265_ML_MODEL_INSTALL_DIR;
    snprintf(dir, size, "%s", installDir);
    if (modelsExistIn(dir))
        return true;
#endif

    x265_log(m_param, X265_LOG_ERROR,
             "ML CTU pred: model files (%s, ...) not found; use --ml-model-dir or the "
             "X265_ML_MODEL_DIR environment variable to locate them\n", s_modelFiles[0]);
    return false;
}

bool MLCTUPredictor::init()
{
    const OrtApiBase* apiBase = OrtGetApiBase();
    m_ort = apiBase ? apiBase->GetApi(ML_ORT_API_VERSION) : NULL;
    if (!m_ort)
    {
        x265_log(m_param, X265_LOG_ERROR, "ML CTU pred: ONNX Runtime %s does not support API version %d\n",
                 apiBase ? apiBase->GetVersionString() : "(unknown)", ML_ORT_API_VERSION);
        return false;
    }

    char modelDir[ML_MAX_PATH];
    if (!resolveModelDir(modelDir, sizeof(modelDir)))
        return false;

    /* Up to mlPoolThreads inferences run concurrently, see Encoder::create() */
    const int mlPoolThreads = X265_MIN(m_param->frameNumThreads, MAX_POOL_THREADS);
    m_intraOpThreads = mlIntraOpThreads(mlPoolThreads, ThreadPool::getCpuCount());
    m_prepThreads    = m_intraOpThreads;

    /* One intra-op pool shared by all sessions */
    OrtThreadingOptions* threadingOptions = NULL;
    if (!checkStatus(m_ort->CreateThreadingOptions(&threadingOptions), "create threading options"))
        return false;
    bool ok = checkStatus(m_ort->SetGlobalIntraOpNumThreads(threadingOptions, m_intraOpThreads), "set intra-op threads") &&
              checkStatus(m_ort->CreateEnvWithGlobalThreadPools(ORT_LOGGING_LEVEL_ERROR, "x265-mlctu", threadingOptions, &m_env), "create environment");
    m_ort->ReleaseThreadingOptions(threadingOptions);
    if (!ok)
        return false;

    if (!checkStatus(m_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &m_memoryInfo), "create memory info"))
        return false;

    for (int i = 0; i < NUM_MODELS; i++)
    {
        char path[ML_MAX_PATH];
        joinPath(path, sizeof(path), modelDir, s_modelFiles[i]);
        m_sessions[i] = loadModel(path);
        if (!m_sessions[i])
            return false;
    }

    x265_log(m_param, X265_LOG_DEBUG, "ML CTU pred: models from %s, %d intra-op thread(s), %d preprocess thread(s)\n",
             modelDir, m_intraOpThreads, m_prepThreads);
    return true;
}

OrtSession* MLCTUPredictor::loadModel(const char* path)
{
    OrtSessionOptions* options = NULL;
    if (!checkStatus(m_ort->CreateSessionOptions(&options), "create session options"))
        return NULL;

    checkStatus(m_ort->DisablePerSessionThreads(options), "disable per-session threads", X265_LOG_WARNING);
    checkStatus(m_ort->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL), "set graph optimization level", X265_LOG_WARNING);
    checkStatus(m_ort->AddSessionConfigEntry(options, "session.intra_op.allow_spinning", "0"), "disable intra-op spinning", X265_LOG_WARNING);
    checkStatus(m_ort->EnableMemPattern(options), "enable memory pattern", X265_LOG_WARNING);
    checkStatus(m_ort->SetSessionExecutionMode(options, ORT_SEQUENTIAL), "set sequential execution mode", X265_LOG_WARNING);

    OrtSession* session = NULL;
    OrtStatus* status;
#ifdef _WIN32
    status = m_ort->CreateSession(m_env, utf8ToWide(path).c_str(), options, &session);
#else
    status = m_ort->CreateSession(m_env, path, options, &session);
#endif
    m_ort->ReleaseSessionOptions(options);

    if (status)
    {
        x265_log(m_param, X265_LOG_ERROR, "ML CTU pred: failed to load model %s: %s\n", path, m_ort->GetErrorMessage(status));
        m_ort->ReleaseStatus(status);
        return NULL;
    }
    return session;
}

void MLCTUPredictor::release()
{
    if (!m_ort)
        return;

    for (int i = 0; i < NUM_MODELS; i++)
    {
        if (m_sessions[i])
            m_ort->ReleaseSession(m_sessions[i]);
        m_sessions[i] = NULL;
    }
    if (m_memoryInfo)
        m_ort->ReleaseMemoryInfo(m_memoryInfo);
    m_memoryInfo = NULL;
    if (m_env)
        m_ort->ReleaseEnv(m_env);
    m_env = NULL;
}

/* Luma normalized to [0,1], per 64x64 block:
 *   b1: 4x4 means minus the 64x64 mean
 *   b2: 2x2 means minus the 32x32 mean
 *   b3: pixels minus the 16x16 mean
 * Pixels outside the picture are zero. */
void MLCTUPredictor::preprocessInput(const pixel* plane, intptr_t stride, MLCTUBuffers& buffers)
{
    const int width = m_param->sourceWidth;
    const int height = m_param->sourceHeight;
    /* fenc is at internal bit depth, not source bit depth */
    const float scale = 1.0f / (float)((1 << X265_DEPTH) - 1);

#pragma omp parallel for schedule(static) num_threads(m_prepThreads)
    for (int blockIdx = 0; blockIdx < m_numBlocks; blockIdx++)
    {
        const int cy = blockIdx / m_numBlocksW;
        const int cx = blockIdx % m_numBlocksW;

        ALIGN_VAR_32(pixel, local[64][64]);

        const bool isFullWidth = (cx + 1) * 64 <= width;
        const bool isFullHeight = (cy + 1) * 64 <= height;

        if (isFullWidth && isFullHeight)
            primitives.cu[BLOCK_64x64].copy_pp(local[0], 64, plane + cy * 64 * stride + cx * 64, stride);
        else
        {
            const int validWidth = X265_MIN(64, width - cx * 64);
            for (int y = 0; y < 64; y++)
            {
                const int py = cy * 64 + y;
                pixel* dst = local[y];

                if (py >= height)
                {
                    memset(dst, 0, 64 * sizeof(pixel));
                    continue;
                }
                memcpy(dst, plane + py * stride + cx * 64, validWidth * sizeof(pixel));
                memset(dst + validWidth, 0, (64 - validWidth) * sizeof(pixel));
            }
        }

        uint32_t sum2x2[32][32];
        for (int y = 0; y < 32; y++)
        {
            const pixel* row0 = local[y * 2];
            const pixel* row1 = local[y * 2 + 1];
            ML_OMP_SIMD
            for (int x = 0; x < 32; x++)
                sum2x2[y][x] = (uint32_t)row0[x * 2] + (uint32_t)row0[x * 2 + 1] +
                               (uint32_t)row1[x * 2] + (uint32_t)row1[x * 2 + 1];
        }

        uint32_t sum4x4[16][16];
        for (int y = 0; y < 16; y++)
        {
            const uint32_t* row0 = sum2x2[y * 2];
            const uint32_t* row1 = sum2x2[y * 2 + 1];
            ML_OMP_SIMD
            for (int x = 0; x < 16; x++)
                sum4x4[y][x] = row0[x * 2] + row0[x * 2 + 1] + row1[x * 2] + row1[x * 2 + 1];
        }

        uint32_t sum8x8[8][8];
        for (int y = 0; y < 8; y++)
        {
            const uint32_t* row0 = sum4x4[y * 2];
            const uint32_t* row1 = sum4x4[y * 2 + 1];
            ML_OMP_SIMD
            for (int x = 0; x < 8; x++)
                sum8x8[y][x] = row0[x * 2] + row0[x * 2 + 1] + row1[x * 2] + row1[x * 2 + 1];
        }

        uint32_t sum16[4][4];
        for (int y = 0; y < 4; y++)
        {
            const uint32_t* row0 = sum8x8[y * 2];
            const uint32_t* row1 = sum8x8[y * 2 + 1];
            for (int x = 0; x < 4; x++)
                sum16[y][x] = row0[x * 2] + row0[x * 2 + 1] + row1[x * 2] + row1[x * 2 + 1];
        }

        uint32_t sum32[2][2];
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 2; x++)
                sum32[y][x] = sum16[y * 2][x * 2] + sum16[y * 2][x * 2 + 1] +
                              sum16[y * 2 + 1][x * 2] + sum16[y * 2 + 1][x * 2 + 1];

        const uint32_t sum64 = sum32[0][0] + sum32[0][1] + sum32[1][0] + sum32[1][1];
        const float mean64 = (float)sum64 * scale / 4096.0f;

        float mean32[2][2], mean16[4][4];
        for (int by = 0; by < 2; by++)
            for (int bx = 0; bx < 2; bx++)
                mean32[by][bx] = (float)sum32[by][bx] * scale / 1024.0f;
        for (int by = 0; by < 4; by++)
            for (int bx = 0; bx < 4; bx++)
                mean16[by][bx] = (float)sum16[by][bx] * scale / 256.0f;

        float* out1 = buffers.b1 + blockIdx * 256;
        const float scaleB1 = scale * 0.0625f;
        for (int by = 0; by < 16; by++)
        {
            ML_OMP_SIMD
            for (int bx = 0; bx < 16; bx++)
                out1[by * 16 + bx] = (float)sum4x4[by][bx] * scaleB1 - mean64;
        }

        float* out2 = buffers.b2 + blockIdx * 1024;
        const float scaleB2 = scale * 0.25f;
        for (int by = 0; by < 32; by++)
        {
            float* dstRow = out2 + by * 32;
            const uint32_t* srcRow = sum2x2[by];
            for (int mx = 0; mx < 2; mx++)
            {
                const float m = mean32[by >> 4][mx];
                ML_OMP_SIMD
                for (int x = 0; x < 16; x++)
                    dstRow[mx * 16 + x] = (float)srcRow[mx * 16 + x] * scaleB2 - m;
            }
        }

        float* out3 = buffers.b3 + blockIdx * 4096;
        for (int y = 0; y < 64; y++)
        {
            const pixel* srcRow = local[y];
            float* dstRow = out3 + y * 64;
            for (int bx = 0; bx < 4; bx++)
            {
                const float m = mean16[y >> 4][bx];
                ML_OMP_SIMD
                for (int x = 0; x < 16; x++)
                    dstRow[bx * 16 + x] = (float)srcRow[bx * 16 + x] * scale - m;
            }
        }
    }
}

void MLCTUPredictor::runModel(OrtSession* session, MLCTUBuffers& buffers, float* output, int blockOffset, int batchCount)
{
    static const char* const inputNames[]  = { "qp", "b1", "b2", "b3" };
    static const char* const outputNames[] = { "out_64", "out_32", "out_16" };

    const int64_t qpShape[1] = { batchCount };
    const int64_t b1Shape[3] = { batchCount, 16, 16 };
    const int64_t b2Shape[3] = { batchCount, 32, 32 };
    const int64_t b3Shape[3] = { batchCount, 64, 64 };

    struct { float* data; size_t count; const int64_t* shape; size_t dims; } inputs[4] =
    {
        { buffers.qp + blockOffset,        (size_t)batchCount,        qpShape, 1 },
        { buffers.b1 + blockOffset * 256,  (size_t)batchCount * 256,  b1Shape, 3 },
        { buffers.b2 + blockOffset * 1024, (size_t)batchCount * 1024, b2Shape, 3 },
        { buffers.b3 + blockOffset * 4096, (size_t)batchCount * 4096, b3Shape, 3 },
    };

    OrtValue* inputTensors[4] = { NULL, NULL, NULL, NULL };
    OrtValue* outputTensors[3] = { NULL, NULL, NULL };
    bool ok = true;

    for (int i = 0; i < 4 && ok; i++)
        ok = checkStatus(m_ort->CreateTensorWithDataAsOrtValue(m_memoryInfo, inputs[i].data, inputs[i].count * sizeof(float),
                                                              inputs[i].shape, inputs[i].dims,
                                                              ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inputTensors[i]),
                         "create input tensor");

    if (ok)
        ok = checkStatus(m_ort->Run(session, NULL, inputNames, (const OrtValue* const*)inputTensors, 4,
                                    outputNames, 3, outputTensors), "run inference");

    float* level[3] = { NULL, NULL, NULL };
    for (int i = 0; i < 3 && ok; i++)
        ok = checkStatus(m_ort->GetTensorMutableData(outputTensors[i], reinterpret_cast<void**>(&level[i])), "read inference output");

    /* On failure write neutral values, leaving the decision to RD */
    if (ok)
        processOutput(level[0], level[1], level[2], output, blockOffset, batchCount);
    else
        processOutput(NULL, NULL, NULL, output, blockOffset, batchCount);

    for (int i = 0; i < 4; i++)
        if (inputTensors[i])
            m_ort->ReleaseValue(inputTensors[i]);
    for (int i = 0; i < 3; i++)
        if (outputTensors[i])
            m_ort->ReleaseValue(outputTensors[i]);
}

/* NULL levels write ML_PRED_NEUTRAL */
void MLCTUPredictor::processOutput(const float* level1, const float* level2, const float* level3, float* output, int blockOffset, int batchCount)
{
    auto prob = [](const float* level, int idx) { return level ? level[idx] : ML_PRED_NEUTRAL; };

    if (m_param->maxCUSize == 32)
    {
        /* Each 64x64 block covers up to 2x2 CTUs */
        const int ctuWidth  = (m_param->sourceWidth  + 31) / 32;
        const int ctuHeight = (m_param->sourceHeight + 31) / 32;

        for (int localIdx = 0; localIdx < batchCount; localIdx++)
        {
            const int blockIdx = blockOffset + localIdx;
            const int row = (blockIdx / m_numBlocksW) * 2;
            const int col = (blockIdx % m_numBlocksW) * 2;

            for (int r = 0; r < 2 && row + r < ctuHeight; r++)
                for (int c = 0; c < 2 && col + c < ctuWidth; c++)
                    output[(row + r) * ctuWidth + col + c] = prob(level2, localIdx * 4 + r * 2 + c);
        }
    }
    else
    {
        for (int localIdx = 0; localIdx < batchCount; localIdx++)
        {
            float* dst = output + CTU_PRED_SIZE * (blockOffset + localIdx);

            dst[0] = prob(level1, localIdx);
            for (int i = 0; i < 4; i++)
            {
                dst[1 + i] = prob(level2, localIdx * 4 + i);

                for (int j = 0; j < 4; j++)
                {
                    const int row = (i / 2) * 2 + (j / 2);
                    const int col = (i % 2) * 2 + (j % 2);
                    const int childIdx = row * 4 + col;
                    dst[5 + s_rasterToZ16[childIdx]] = prob(level3, localIdx * 16 + childIdx);
                }
            }
        }
    }
}

void MLCTUPredictor::fillQpBuffer(int qp, const double* cuTreeOffsets, uint32_t qgSize, MLCTUBuffers& buffers)
{
    const int width = m_param->sourceWidth;
    const int height = m_param->sourceHeight;
    const int blockSize = (cuTreeOffsets && qgSize > 0) ? (int)qgSize : 16;
    const int widthInBlocks = (width + blockSize - 1) / blockSize;

#pragma omp parallel for schedule(static) num_threads(m_prepThreads)
    for (int blockIdx = 0; blockIdx < m_numBlocks; blockIdx++)
    {
        int blockQp = qp;

        /* Mean cuTree QP offset over the block */
        if (cuTreeOffsets)
        {
            const int startY = (blockIdx / m_numBlocksW) * 64;
            const int startX = (blockIdx % m_numBlocksW) * 64;
            const int endY = X265_MIN(startY + 64, height);
            const int endX = X265_MIN(startX + 64, width);
            double sum = 0.0;
            int count = 0;

            for (int y = startY; y < endY; y += blockSize)
                for (int x = startX; x < endX; x += blockSize)
                {
                    sum += cuTreeOffsets[(y / blockSize) * widthInBlocks + (x / blockSize)];
                    count++;
                }

            if (count > 0)
                blockQp = x265_clip3(QP_MIN, QP_MAX_SPEC, (int)(qp + sum / count + 0.5));
        }

        buffers.qp[blockIdx] = blockQp / (float)QP_MAX_SPEC; // model expects [0,1]
    }
}

/* Publishes rowsReady per chunk so WPP can start before the frame is done */
void MLCTUPredictor::predictPreparedPartitionsChunked(int qp, const double* cuTreeOffsets, uint32_t qgSize, float* output,
                                                      MLCTUBuffers& buffers, ThreadSafeInteger* rowsReady, int numActualRows)
{
    fillQpBuffer(qp, cuTreeOffsets, qgSize, buffers);

    OrtSession* session = m_sessions[mlSessionForQP(qp)];
    const int ctuRowsPerBlockRow = ML_BLOCK_SIZE / m_param->maxCUSize;

    for (int blockRow = 0; blockRow < m_numBlocksH; blockRow += ML_ROW_CHUNK_SIZE)
    {
        const int chunkRows = X265_MIN(ML_ROW_CHUNK_SIZE, m_numBlocksH - blockRow);
        runModel(session, buffers, output, blockRow * m_numBlocksW, chunkRows * m_numBlocksW);

        if (rowsReady)
            rowsReady->set(X265_MIN((blockRow + chunkRows) * ctuRowsPerBlockRow, numActualRows));
    }

    if (rowsReady)
        rowsReady->set(numActualRows);
}

void MLCTUPredictor::enqueue(MLPredictionRequest* req)
{
    m_queueLock.acquire();
    m_requestQueue.push(req);
    m_helpWanted = true;
    m_queueLock.release();
    tryWakeOne();
}

bool MLCTUPredictor::enqueuePreprocessRequest(MLPredictionRequest* req)
{
    if (!m_pool)
        return false;

    req->type = ML_PREPROCESS;
    req->preprocessQueued = true;
    req->preprocessDone.reset();
    enqueue(req);
    return true;
}

void MLCTUPredictor::enqueuePreparedRequest(MLPredictionRequest* req)
{
    req->type = ML_PREDICT_PREPARED;
    if (m_pool)
        enqueue(req);
    else
    {
        predictPreparedPartitionsChunked(req->qp, req->cuTreeOffsets, req->qgSize, req->output, *req->buffers,
                                         req->rowsReady, req->numActualRows);
        req->done.trigger();
    }
}

void MLCTUPredictor::enqueueRequest(MLPredictionRequest* req)
{
    req->type = ML_PREDICT_FULL;
    if (m_pool)
        enqueue(req);
    else
    {
        preprocessInput(req->plane, req->stride, *req->buffers);
        predictPreparedPartitionsChunked(req->qp, req->cuTreeOffsets, req->qgSize, req->output, *req->buffers,
                                         req->rowsReady, req->numActualRows);
        req->done.trigger();
    }
}

void MLCTUPredictor::findJob(int /*workerThreadId*/)
{
    MLPredictionRequest* req = NULL;
    m_queueLock.acquire();
    if (!m_requestQueue.empty())
    {
        req = m_requestQueue.front();
        m_requestQueue.pop();
    }
    m_helpWanted = !m_requestQueue.empty();
    m_queueLock.release();

    if (!req)
        return;

    if (req->type == ML_PREPROCESS)
    {
        preprocessInput(req->plane, req->stride, *req->buffers);
        req->preprocessDone.trigger();
        return;
    }

    if (req->type == ML_PREDICT_FULL)
        preprocessInput(req->plane, req->stride, *req->buffers);
    predictPreparedPartitionsChunked(req->qp, req->cuTreeOffsets, req->qgSize, req->output, *req->buffers,
                                     req->rowsReady, req->numActualRows);
    req->done.trigger();
}
}

#endif // ENABLE_MLCTUPRED
