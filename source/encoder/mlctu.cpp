/*****************************************************************************
 * Copyright (C) 2013-2020 MulticoreWare, Inc
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
 *****************************************************************************/

#ifdef ENABLE_MLCTUPRED

#include "mlctu.h"
#include "common.h"
#include "primitives.h"

#include <omp.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <chrono>


#ifdef _WIN32
#include <windows.h>
#endif

/* QP range -> model index: [1-15]=0, [16-35]=1, [36-51]=2.
 * Model expects qp pre-normalized to [0,1] (qp/51); see qp_buffer fill below. */
#if USE_QUANTIZED_MODEL
const char* model_paths[NUM_MODELS] =
{
    ONNX_MODEL_PATH "/eth_cnn_qp1-15_quant.onnx",
    ONNX_MODEL_PATH "/eth_cnn_qp16-35_quant.onnx",
    ONNX_MODEL_PATH "/eth_cnn_qp36-51_quant.onnx"
};
#else
const char* model_paths[NUM_MODELS] =
{
    ONNX_MODEL_PATH "/eth_cnn_qp1-15.onnx",
    ONNX_MODEL_PATH "/eth_cnn_qp16-35.onnx",
    ONNX_MODEL_PATH "/eth_cnn_qp36-51.onnx"
};
#endif

/* QP range → model index, matching model_paths[] above. */
static int mlSessionForQP(int qp)
{
    if (qp <= 15)
        return 0;
    if (qp <= 35)
        return 1;
    return 2;
}

/* Raster-to-Z-scan for the 16 4x4 sub-blocks within a 16x16 block */
static const uint8_t rasterToZ16[16] =
{
     0,  1,  4,  5,
     2,  3,  6,  7,
     8,  9, 12, 13,
    10, 11, 14, 15
};

namespace X265_NS {

/* Threads per inference: an even share of the machine across the poolWidth
 * concurrent inferences, capped at ML_MAX_INTRAOP_THREADS and floored at 1, so
 * big boxes stay wide and small ones back off (AGENTS.md §7.2). */
static int mlIntraOpThreads(int poolWidth, int cpuCount)
{
    if (poolWidth < 1)
        poolWidth = 1;
    return x265_clip3(1, ML_MAX_INTRAOP_THREADS, cpuCount / poolWidth);
}

MLCTUPredictor::MLCTUPredictor(x265_param* param)
    : m_param(param)
    , maxCUSize(0)
    , totalCTUs(0)
    , m_intraOpThreads(ML_MAX_INTRAOP_THREADS)
    , m_prepThreads(ML_MAX_INTRAOP_THREADS)
    , m_env(NULL)
    , m_sessions()
{}

MLCTUPredictor::~MLCTUPredictor()
{
    cleanup_ctu_onnx();
}

bool MLCTUPredictor::init()
{
    this->maxCUSize = m_param->maxCUSize;
    const int numCTUsInWidth = (m_param->sourceWidth + 64 - 1) / 64;
    const int numCTUsInHeight = (m_param->sourceHeight + 64 - 1) / 64;
    this->totalCTUs = numCTUsInWidth * numCTUsInHeight;

    /* Size the ML thread budget to the machine: up to mlPoolThreads inferences
     * run at once (ML pool, encoder.cpp), each m_intraOpThreads wide. */
    const int mlPoolThreads = X265_MIN(m_param->frameNumThreads, MAX_POOL_THREADS);
    m_intraOpThreads = mlIntraOpThreads(mlPoolThreads, ThreadPool::getCpuCount());
    m_prepThreads    = m_intraOpThreads;
    x265_log(m_param, X265_LOG_INFO,
             "ML CTU pred: intra-op threads/inference=%d, preprocess threads=%d, "
             "ml-pool workers~%d (=> up to %d ML threads active), row-chunk=%d block-row(s)\n",
             m_intraOpThreads, m_prepThreads, mlPoolThreads,
             mlPoolThreads * m_intraOpThreads, X265_MAX(1, ML_ROW_CHUNK_SIZE));

    const OrtApi* ort = OrtGetApiBase()->GetApi(17);

    // Shared intra-op pool across all sessions (each opts in via DisablePerSessionThreads)
    // since only one session runs at a time.
    OrtThreadingOptions* threadingOptions = nullptr;
    OrtStatus* status = ort->CreateThreadingOptions(&threadingOptions);
    if (status)
    {
        x265_log(m_param, X265_LOG_ERROR, "ML CTU pred: failed to create ONNX threading options\n");
        ort->ReleaseStatus(status);
        return false;
    }
    status = ort->SetGlobalIntraOpNumThreads(threadingOptions, m_intraOpThreads);
    if (status)
    {
        x265_log(m_param, X265_LOG_ERROR, "ML CTU pred: failed to set global intra-op threads\n");
        ort->ReleaseStatus(status);
        ort->ReleaseThreadingOptions(threadingOptions);
        return false;
    }

    status = ort->CreateEnvWithGlobalThreadPools(
        ORT_LOGGING_LEVEL_ERROR, "ctu_inference", threadingOptions, &m_env);
    ort->ReleaseThreadingOptions(threadingOptions);
    if (status)
    {
        x265_log(m_param, X265_LOG_ERROR, "ML CTU pred: failed to create ONNX env\n");
        ort->ReleaseStatus(status);
        return false;
    }
    /* Initialise all NUM_MODELS sessions */
    for (int i = 0; i < NUM_MODELS; i++)
    {
        m_sessions[i] = init_ctu_onnx(model_paths[i]);
        if (!m_sessions[i])
        {
            x265_log(m_param, X265_LOG_ERROR,
                     "ML CTU pred: failed to load model %s\n", model_paths[i]);
            return false;
        }
    }
    return true;
}

CTUPartitionInference* MLCTUPredictor::init_ctu_onnx(const char* model_path)
{
    if (!model_path) {
        printf("Error: Model path is NULL\n");
        return NULL;
    }

    CTUPartitionInference* ctu = X265_MALLOC(CTUPartitionInference, 1);
    OrtStatus* status = nullptr;

    if (!ctu) {
        printf("Error: Failed to allocate memory for CTUPartitionInference\n");
        return NULL;
    }
    ctu->ort = OrtGetApiBase()->GetApi(17);
    if (!ctu->ort) {
        printf("Error: Failed to get ONNX Runtime API\n");
        X265_FREE(ctu);
        return NULL;
    }
    ctu->env = this->m_env;
    status = ctu->ort->CreateSessionOptions(&ctu->session_options);
    if (status != NULL) {
        const char* msg = ctu->ort->GetErrorMessage(status);
        printf("Error: Failed to create session options: %s\n", msg);
        ctu->ort->ReleaseStatus(status);
        X265_FREE(ctu);
        return NULL;
    }
#if ENABLE_ORT_PROFILE
    status = ctu->ort->EnableProfiling(ctu->session_options, "onnx_profile");
    if (status != NULL)
    {
        printf("Failed to enable profiling: %s\n",
            ctu->ort->GetErrorMessage(status));
        ctu->ort->ReleaseStatus(status);
    }
#endif
    /* Each session shares one intra-op pool set up on m_env in init(). */
    auto warnIfFailed = [&](OrtStatus* s, const char* what) {
        if (s != NULL) {
            printf("Warning: failed to %s: %s\n", what, ctu->ort->GetErrorMessage(s));
            ctu->ort->ReleaseStatus(s);
        }
    };
    warnIfFailed(ctu->ort->DisablePerSessionThreads(ctu->session_options), "disable per-session threads");
    warnIfFailed(ctu->ort->SetSessionGraphOptimizationLevel(ctu->session_options, ORT_ENABLE_ALL), "set graph optimization level");
    warnIfFailed(ctu->ort->AddSessionConfigEntry(ctu->session_options, "session.intra_op.allow_spinning", "0"), "set intra-op spinning config");
    warnIfFailed(ctu->ort->EnableMemPattern(ctu->session_options), "enable mem pattern");
    warnIfFailed(ctu->ort->SetSessionExecutionMode(ctu->session_options, ORT_SEQUENTIAL), "set sequential execution mode");

    status = ctu->ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &ctu->memory_info);
    if (status != NULL) {
        printf("Warning: Failed to create memory: %s\n", ctu->ort->GetErrorMessage(status));
        ctu->ort->ReleaseStatus(status);
    }
#ifdef _WIN32
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, model_path, -1, NULL, 0);
    std::wstring wide_model_path(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, model_path, -1, &wide_model_path[0], size_needed);
    status = ctu->ort->CreateSession(ctu->env, wide_model_path.c_str(), ctu->session_options, &ctu->session);

#else
    status = ctu->ort->CreateSession(ctu->env, model_path, ctu->session_options, &ctu->session);
#endif
    if (status != NULL) {
        const char* msg = ctu->ort->GetErrorMessage(status);
        printf("Error: Failed to create session: %s\n", msg);
        ctu->ort->ReleaseStatus(status);
        ctu->ort->ReleaseSessionOptions(ctu->session_options);
        if (ctu->memory_info)
            ctu->ort->ReleaseMemoryInfo(ctu->memory_info);
        X265_FREE(ctu);
        return NULL;
    }
    return ctu;
}

void MLCTUPredictor::cleanup_ctu_onnx()
{
    for (int i = 0; i < NUM_MODELS; i++)
    {
        CTUPartitionInference* ctu = m_sessions[i];

        if (!ctu)
            continue;
#if ENABLE_ORT_PROFILE
        if (ctu->session)
        {
            OrtAllocator* allocator = nullptr;
            ctu->ort->GetAllocatorWithDefaultOptions(&allocator);

            char* profile_file = nullptr;

            OrtStatus* status = ctu->ort->SessionEndProfiling(
                ctu->session,
                allocator,
                &profile_file);

            if (status == NULL)
            {
                printf("ONNX profile saved to: %s\n", profile_file);

                allocator->Free(allocator, profile_file);
            }
            else
            {
                printf("SessionEndProfiling failed: %s\n",
                    ctu->ort->GetErrorMessage(status));
                ctu->ort->ReleaseStatus(status);
            }

            ctu->ort->ReleaseSession(ctu->session);
        }
#else
        if (ctu->session)
            ctu->ort->ReleaseSession(ctu->session);
#endif
        if (ctu->session_options)
            ctu->ort->ReleaseSessionOptions(ctu->session_options);

        if (ctu->memory_info)
            ctu->ort->ReleaseMemoryInfo(ctu->memory_info);

        X265_FREE(ctu);

        m_sessions[i] = nullptr;
    }

    if (m_env)
    {
        OrtGetApiBase()->GetApi(17)->ReleaseEnv(m_env);
        m_env = nullptr;
    }
}

void MLCTUPredictor::preprocessInput(pixel* plane, intptr_t stride, MLCTUBuffers& buffers)
{
#if PROFILE_ML
    auto t0 = std::chrono::high_resolution_clock::now();
#endif
    const int width = m_param->sourceWidth;
    const int height = m_param->sourceHeight;
    const int depth = m_param->sourceBitDepth;
    const int numCTUsInWidth = (width + 63) / 64;

#pragma omp parallel for schedule(static) num_threads(m_prepThreads)
    for (int ctuIdx = 0; ctuIdx < totalCTUs; ctuIdx++)
    {
        const int cy = ctuIdx / numCTUsInWidth;
        const int cx = ctuIdx % numCTUsInWidth;

        ALIGN_VAR_32(pixel, local[64][64]);

        const pixel* plane_pixel = static_cast<const pixel*>(plane);
        const bool isFullWidth = (cx + 1) * 64 <= width;
        const bool isFullHeight = (cy + 1) * 64 <= height;

        if (isFullWidth && isFullHeight)
        {
            const pixel* src = plane_pixel + cy * 64 * stride + cx * 64;
            primitives.cu[BLOCK_64x64].copy_pp(local[0], 64, src, stride);
        }
        else
        {
            for (int y = 0; y < 64; y++) {
                const int py = cy * 64 + y;
                pixel* dst = local[y];

                if (py >= height) {
                    memset(dst, 0, 64 * sizeof(pixel));
                    continue;
                }

                if (isFullWidth) {
                    const pixel* src = plane_pixel + py * stride + cx * 64;
                    memcpy(dst, src, 64 * sizeof(pixel));
                } else {
                    const pixel* src = plane_pixel + py * stride + cx * 64;
                    int remaining = width - cx * 64;
                    memcpy(dst, src, remaining * sizeof(pixel));
                    memset(dst + remaining, 0, (64 - remaining) * sizeof(pixel));
                }
            }
        }
        uint32_t sum2x2[32][32];
        for (int y = 0; y < 32; y++) {
            const pixel* row0 = local[y * 2];
            const pixel* row1 = local[y * 2 + 1];
            #pragma omp simd
            for (int x = 0; x < 32; x++) {
                sum2x2[y][x] = (uint32_t)row0[x * 2] + (uint32_t)row0[x * 2 + 1] +
                               (uint32_t)row1[x * 2] + (uint32_t)row1[x * 2 + 1];
            }
        }

        uint32_t sum4x4[16][16];
        for (int y = 0; y < 16; y++) {
            const uint32_t* row0 = sum2x2[y * 2];
            const uint32_t* row1 = sum2x2[y * 2 + 1];
            #pragma omp simd
            for (int x = 0; x < 16; x++) {
                sum4x4[y][x] = row0[x * 2] + row0[x * 2 + 1] +
                               row1[x * 2] + row1[x * 2 + 1];
            }
        }

        uint32_t sum8x8[8][8];
        for (int y = 0; y < 8; y++) {
            const uint32_t* row0 = sum4x4[y * 2];
            const uint32_t* row1 = sum4x4[y * 2 + 1];
            #pragma omp simd
            for (int x = 0; x < 8; x++) {
                sum8x8[y][x] = row0[x * 2] + row0[x * 2 + 1] +
                               row1[x * 2] + row1[x * 2 + 1];
            }
        }

        uint32_t sum16[4][4];
        for (int y = 0; y < 4; y++) {
            const uint32_t* row0 = sum8x8[y * 2];
            const uint32_t* row1 = sum8x8[y * 2 + 1];
            #pragma omp simd
            for (int x = 0; x < 4; x++) {
                sum16[y][x] = row0[x * 2] + row0[x * 2 + 1] +
                              row1[x * 2] + row1[x * 2 + 1];
            }
        }

        uint32_t sum32[2][2];
        for (int y = 0; y < 2; y++) {
            const uint32_t* row0 = sum16[y * 2];
            const uint32_t* row1 = sum16[y * 2 + 1];
            #pragma omp simd
            for (int x = 0; x < 2; x++) {
                sum32[y][x] = row0[x * 2] + row0[x * 2 + 1] +
                              row1[x * 2] + row1[x * 2 + 1];
            }
        }

        uint32_t global_sum = sum32[0][0] + sum32[0][1] + sum32[1][0] + sum32[1][1];

        const float scale = 1.0f /(float)((1<< depth ) - 1);
        const float global_mean = (float)global_sum * scale / 4096.0f;
        float mean32[2][2], mean16[4][4];

        for (int by = 0; by < 2; by++) {
            for (int bx = 0; bx < 2; bx++) {
                mean32[by][bx] = (float)sum32[by][bx] * scale / 1024.0f;
            }
        }

        for (int by = 0; by < 4; by++) {
            for (int bx = 0; bx < 4; bx++) {
                mean16[by][bx] = (float)sum16[by][bx] * scale / 256.0f;
            }
        }

        float *out1 = buffers.b1 + ctuIdx * 256;
        const float scale_b1 = scale * 0.0625f;
        for (int by = 0; by < 16; by++) {
            #pragma omp simd
            for (int bx = 0; bx < 16; bx++) {
                out1[by*16 + bx] = (float)sum4x4[by][bx] * scale_b1 - global_mean;
            }
        }

        float *out2 = buffers.b2 + ctuIdx * 1024;
        const float scale_b2 = scale * 0.25f;
        for (int by = 0; by < 32; by++) {
            float* dstRow = out2 + by * 32;
            const uint32_t* srcRow = sum2x2[by];
            int my = by >> 4;
            for (int mx = 0; mx < 2; mx++) {
                float m = mean32[my][mx];
                #pragma omp simd
                for (int x = 0; x < 16; x++) {
                    dstRow[mx * 16 + x] = (float)srcRow[mx * 16 + x] * scale_b2 - m;
                }
            }
        }

        float *out3 = buffers.b3 + ctuIdx * 4096;
        for (int y = 0; y < 64; y++) {
            const pixel* srcRow = local[y];
            float* dstRow = out3 + y * 64;
            int by = y >> 4;
            for (int bx = 0; bx < 4; bx++) {
                float m = mean16[by][bx];
                #pragma omp simd
                for (int x = 0; x < 16; x++) {
                    dstRow[bx * 16 + x] = (float)srcRow[bx * 16 + x] * scale - m;
                }
            }
        }
    }
#if PROFILE_ML
    auto t1 = std::chrono::high_resolution_clock::now();
    double prep_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    static int prepCount = 0;
    static double totalPrep = 0.0;
    prepCount++;
    totalPrep += prep_ms;
    printf("ML CTU preprocess frame %d: preprocess=%.2f ms (avg %.2f)\n",
           prepCount, prep_ms, totalPrep / prepCount);
#endif
}

void MLCTUPredictor::run_model(CTUPartitionInference* ctu, MLCTUBuffers& buffers, float* output, int ctuOffset, int batchCount)
{
#if PROFILE_ML
    auto t_total0 = std::chrono::high_resolution_clock::now();
    auto t_tensor0 = t_total0;
#endif
    OrtStatus* status;
    OrtValue* input_tensor_qp = nullptr;
    OrtValue* input_tensor_b1 = nullptr;
    OrtValue* input_tensor_b2 = nullptr;
    OrtValue* input_tensor_b3 = nullptr;
    OrtValue* output_tensor[3] = { nullptr, nullptr, nullptr };
    bool ok = true;

    auto makeTensor = [&](float* data, size_t bytes, const int64_t* shape, size_t dims, OrtValue** out, const char* name) {
        status = ctu->ort->CreateTensorWithDataAsOrtValue(ctu->memory_info, data, bytes, shape, dims,
                                                           ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, out);
        if (status)
        {
            printf("Error creating %s tensor: %s\n", name, ctu->ort->GetErrorMessage(status));
            ctu->ort->ReleaseStatus(status);
            ok = false;
        }
    };

    const int64_t qp_shape[1] = { batchCount };
    makeTensor(buffers.qp_buffer + ctuOffset, batchCount * sizeof(float), qp_shape, 1, &input_tensor_qp, "qp");

    const int64_t b1_shape[3] = { batchCount, 16, 16 };
    makeTensor(buffers.b1 + ctuOffset * 256, batchCount * 16 * 16 * sizeof(float), b1_shape, 3, &input_tensor_b1, "b1");

    const int64_t b2_shape[3] = { batchCount, 32, 32 };
    makeTensor(buffers.b2 + ctuOffset * 1024, batchCount * 32 * 32 * sizeof(float), b2_shape, 3, &input_tensor_b2, "b2");

    const int64_t b3_shape[3] = { batchCount, 64, 64 };
    makeTensor(buffers.b3 + ctuOffset * 4096, batchCount * 64 * 64 * sizeof(float), b3_shape, 3, &input_tensor_b3, "b3");

    const char* input_names[]  = { "qp", "b1", "b2", "b3" };
    const char* output_names[] = { "out_64", "out_32", "out_16" };

    OrtValue* input_tensors[4] = {
        input_tensor_qp,
        input_tensor_b1,
        input_tensor_b2,
        input_tensor_b3
    };
#if PROFILE_ML
    auto t_tensor1 = std::chrono::high_resolution_clock::now();
    auto t_run0 = t_tensor1;
#endif

    if (ok)
    {
        status = ctu->ort->Run(ctu->session, nullptr,
            input_names,
            (const OrtValue* const*)input_tensors,
            4,
            output_names,
            3,
            output_tensor
        );

        if (status)
        {
            printf("Error running ONNX model: %s\n", ctu->ort->GetErrorMessage(status));
            ctu->ort->ReleaseStatus(status);
            ok = false;
        }
    }
#if PROFILE_ML
    auto t_run1 = std::chrono::high_resolution_clock::now();
    auto t_access0 = t_run1;
#endif
    float* raw0 = nullptr;
    float* raw1 = nullptr;
    float* raw2 = nullptr;

    if (ok)
    {
        status = ctu->ort->GetTensorMutableData(output_tensor[0], reinterpret_cast<void**>(&raw0));
        if (!status)
            status = ctu->ort->GetTensorMutableData(output_tensor[1], reinterpret_cast<void**>(&raw1));
        if (!status)
            status = ctu->ort->GetTensorMutableData(output_tensor[2], reinterpret_cast<void**>(&raw2));
        if (status)
        {
           fprintf(stderr, "GetTensorMutableData failed\n");
            ctu->ort->ReleaseStatus(status);
            ok = false;
        }
    }

#if PROFILE_ML
    auto t_access1 = std::chrono::high_resolution_clock::now();
    auto t_post0 = t_access1;
#endif
    if (ok)
        process_output(raw0, raw1, raw2, output, ctuOffset, batchCount);

#if PROFILE_ML
    auto t_post1 = std::chrono::high_resolution_clock::now();
    auto t_cleanup0 = t_post1;
#endif
    ctu->ort->ReleaseValue(input_tensor_qp);
    ctu->ort->ReleaseValue(input_tensor_b1);
    ctu->ort->ReleaseValue(input_tensor_b2);
    ctu->ort->ReleaseValue(input_tensor_b3);

    ctu->ort->ReleaseValue(output_tensor[0]);
    ctu->ort->ReleaseValue(output_tensor[1]);
    ctu->ort->ReleaseValue(output_tensor[2]);

#if PROFILE_ML
    auto t_cleanup1 = std::chrono::high_resolution_clock::now();
    double tensor_ms = std::chrono::duration<double, std::milli>(t_tensor1 - t_tensor0).count();
    double run_ms = std::chrono::duration<double, std::milli>(t_run1 - t_run0).count();
    double access_ms = std::chrono::duration<double, std::milli>(t_access1 - t_access0).count();
    double post_ms = std::chrono::duration<double, std::milli>(t_post1 - t_post0).count();
    double cleanup_ms = std::chrono::duration<double, std::milli>(t_cleanup1 - t_cleanup0).count();
    double total_ms = std::chrono::duration<double, std::milli>(t_cleanup1 - t_total0).count();
    static int runCount = 0;
    static double totalTensor = 0.0;
    static double totalRun = 0.0;
    static double totalAccess = 0.0;
    static double totalPost = 0.0;
    static double totalCleanup = 0.0;
    static double totalModel = 0.0;
    runCount++;
    totalTensor += tensor_ms;
    totalRun += run_ms;
    totalAccess += access_ms;
    totalPost += post_ms;
    totalCleanup += cleanup_ms;
    totalModel += total_ms;
    printf("ML CTU run_model frame %d: tensor_create=%.2f ms (avg %.2f), run_inf=%.2f ms (avg %.2f), output_access=%.2f ms (avg %.2f), post_proc=%.2f ms (avg %.2f), cleanup=%.2f ms (avg %.2f), total=%.2f ms (avg %.2f)\n",
           runCount,
           tensor_ms, totalTensor / runCount,
           run_ms, totalRun / runCount,
           access_ms, totalAccess / runCount,
           post_ms, totalPost / runCount,
           cleanup_ms, totalCleanup / runCount,
           total_ms, totalModel / runCount);
#endif
}

void MLCTUPredictor::process_output(const float* level_1, const float* level_2, const float* level_3, float* output, int ctuOffset, int batchCount)
{
    int maxCUsize = m_param->maxCUSize;

    if (maxCUsize == 32)
    {
        // Actual CTU dimensions (since maxCUsize=32)
        const int ctuWidth = (m_param->sourceWidth) / 32;

        // Virtual CTU dimensions (half of actual)
        const int vCtuWidth = ctuWidth / 2;

        for (int localVn = 0; localVn < batchCount; localVn++)
        {
            int vn = ctuOffset + localVn;

            // ===== GET VIRTUAL CTU 2D POSITION =====
            int vrow = vn / vCtuWidth;
            int vcol = vn % vCtuWidth;

            // ===== CALCULATE 4 ACTUAL CTU ADDRESSES (raster order) =====
            // Virtual CTU (vrow, vcol) maps to 4 actual CTUs:
            int addr_Q0 = (2*vrow + 0) * ctuWidth + (2*vcol + 0);  // TL
            int addr_Q1 = (2*vrow + 0) * ctuWidth + (2*vcol + 1);  // TR
            int addr_Q2 = (2*vrow + 1) * ctuWidth + (2*vcol + 0);  // BL
            int addr_Q3 = (2*vrow + 1) * ctuWidth + (2*vcol + 1);  // BR
            int addrs[4] = {addr_Q0, addr_Q1, addr_Q2, addr_Q3};
            // ===== WRITE TO ALL 4 ACTUAL CTU POSITIONS =====
            for (int q = 0; q < 4; q++)
            {
                float* dst = output + addrs[q];

                // 32→16 split probability (from level_2); analysis.cpp applies
                // the split-confident and no-split-confident thresholds.
                dst[0] = level_2[localVn * 4 + q];
            }
        }
    }
    else  // maxCUsize == 64
    {
        for (int localN = 0; localN < batchCount; localN++)
        {
            int n = ctuOffset + localN;
            float* dst = output + 21 * n;

            // Store raw split probabilities; analysis.cpp applies the
            // split-confident and no-split-confident thresholds.
            dst[0] = level_1[localN];

            for (int i = 0; i < 4; i++)
            {
                dst[1 + i] = level_2[localN * 4 + i];

                for (int j = 0; j < 4; j++)
                {
                    int row = (i / 2) * 2 + (j / 2);
                    int col = (i % 2) * 2 + (j % 2);
                    int childIdx = row * 4 + col;
                    int z = rasterToZ16[childIdx];

                    dst[5 + z] = level_3[localN * 16 + childIdx];
                }
            }
        }
    }
}

void MLCTUPredictor::fillQpBuffer(int QP, const double* cuTreeOffsets, uint32_t qgSize, MLCTUBuffers& buffers)
{
    const int width = m_param->sourceWidth;
    const int height = m_param->sourceHeight;
    const int numCTUsInWidth = (width + 63) / 64;
    const int blockSize = (cuTreeOffsets && qgSize > 0) ? (int)qgSize : 16;
    const int widthInBlocks = (width + blockSize - 1) / blockSize;

#pragma omp parallel for schedule(static) num_threads(m_prepThreads)
    for (int ctuIdx = 0; ctuIdx < totalCTUs; ctuIdx++)
    {
        const int cy = ctuIdx / numCTUsInWidth;
        const int cx = ctuIdx % numCTUsInWidth;
        int perCtuQP = QP;

        if (cuTreeOffsets)
        {
            double sum = 0.0;
            int count = 0;
            const int startY = cy * 64;
            const int startX = cx * 64;
            const int endY = X265_MIN(startY + 64, height);
            const int endX = X265_MIN(startX + 64, width);

            for (int y = startY; y < endY; y += blockSize)
            {
                for (int x = startX; x < endX; x += blockSize)
                {
                    const int idx = (y / blockSize) * widthInBlocks + (x / blockSize);
                    sum += cuTreeOffsets[idx];
                    count++;
                }
            }

            if (count > 0)
                perCtuQP = x265_clip3(QP_MIN, QP_MAX_SPEC, (int)(QP + sum / count + 0.5));
        }

        // Model expects qp pre-normalized to [0,1] (qp/51).
        buffers.qp_buffer[ctuIdx] = perCtuQP / 51.0f;
    }
}

void MLCTUPredictor::predictPreparedPartitions(int QP, const double* cuTreeOffsets, uint32_t qgSize, float* output, MLCTUBuffers& buffers)
{
#if PROFILE_ML
    auto t_total0 = std::chrono::high_resolution_clock::now();
    auto t_qp0 = t_total0;
#endif
    fillQpBuffer(QP, cuTreeOffsets, qgSize, buffers);

#if PROFILE_ML
    auto t_qp1 = std::chrono::high_resolution_clock::now();
#endif
    int session_id = mlSessionForQP(QP);

    run_model(m_sessions[session_id], buffers, output, 0, totalCTUs);
#if PROFILE_ML
    auto t_total1 = std::chrono::high_resolution_clock::now();
    double qp_ms = std::chrono::duration<double, std::milli>(t_qp1 - t_qp0).count();
    double total_ms = std::chrono::duration<double, std::milli>(t_total1 - t_total0).count();
    static int predictCount = 0;
    static double totalQp = 0.0;
    static double totalPredict = 0.0;
    predictCount++;
    totalQp += qp_ms;
    totalPredict += total_ms;
    printf("ML CTU predict frame %d: qp_buffer=%.2f ms (avg %.2f), predict_total=%.2f ms (avg %.2f)\n",
           predictCount, qp_ms, totalQp / predictCount, total_ms, totalPredict / predictCount);
#endif
}

void MLCTUPredictor::predictPreparedPartitionsChunked(int QP, const double* cuTreeOffsets, uint32_t qgSize, float* output,
                                                       MLCTUBuffers& buffers, ThreadSafeInteger* rowsReady, int numActualRows)
{
    const int numCTUsInWidth = (m_param->sourceWidth + 63) / 64;
    const int numCTUsInHeight = (m_param->sourceHeight + 63) / 64;

    fillQpBuffer(QP, cuTreeOffsets, qgSize, buffers);

    int session_id = mlSessionForQP(QP);

    // Run in row chunks and publish rowsReady per chunk so WPP need not wait on the whole frame.
    const int rowsPerBlockRow = (m_param->maxCUSize == 32) ? 2 : 1;
    const int blockRowsPerChunk = X265_MAX(1, ML_ROW_CHUNK_SIZE);

    for (int blockRow = 0; blockRow < numCTUsInHeight; blockRow += blockRowsPerChunk)
    {
        const int chunkBlockRows = X265_MIN(blockRowsPerChunk, numCTUsInHeight - blockRow);
        const int ctuOffset = blockRow * numCTUsInWidth;
        const int batchCount = chunkBlockRows * numCTUsInWidth;
        run_model(m_sessions[session_id], buffers, output, ctuOffset, batchCount);

        if (rowsReady)
        {
            int readyRows = X265_MIN((blockRow + chunkBlockRows) * rowsPerBlockRow, numActualRows);
            rowsReady->set(readyRows);
        }
    }

    if (rowsReady)
        rowsReady->set(numActualRows);
}

void MLCTUPredictor::predictPartitions(pixel* plane, intptr_t stride, int QP, const double* cuTreeOffsets, uint32_t qgSize, float* output, MLCTUBuffers& buffers)
{
#if PROFILE_ML
    auto t0 = std::chrono::high_resolution_clock::now();
#endif
    preprocessInput(plane, stride, buffers);
    predictPreparedPartitions(QP, cuTreeOffsets, qgSize, output, buffers);
#if PROFILE_ML
    auto t1 = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    static int frameCount = 0;
    static double totalTime = 0.0;
    frameCount++;
    totalTime += total_ms;
    printf("ML CTU sync frame %d: total=%.2f ms (avg %.2f)\n",
           frameCount, total_ms, totalTime / frameCount);
#endif
}

static void enqueueMLRequest(MLCTUPredictor* predictor, MLPredictionRequest* req)
{
    predictor->m_queueLock.acquire();
    predictor->m_requestQueue.push(req);
    predictor->m_helpWanted = true;
    predictor->m_queueLock.release();
    predictor->tryWakeOne();
}

bool MLCTUPredictor::enqueuePreprocessRequest(MLPredictionRequest* req)
{
    if (!m_pool)
        return false;

    req->type = ML_PREPROCESS;
    req->preprocessQueued = true;
    req->preprocessDone.reset();
    enqueueMLRequest(this, req);
    return true;
}

void MLCTUPredictor::enqueuePreparedRequest(MLPredictionRequest* req)
{
    req->type = ML_PREDICT_PREPARED;
    if (!m_pool)
    {
        predictPreparedPartitions(req->qp, req->cuTreeOffsets, req->qgSize, req->output, *req->buffers);
        if (req->rowsReady)
            req->rowsReady->set(req->numActualRows);
        req->done.trigger();
        return;
    }

    enqueueMLRequest(this, req);
}

void MLCTUPredictor::enqueueRequest(MLPredictionRequest* req)
{
    req->type = ML_PREDICT_FULL;
    if (!m_pool)
    {
        predictPartitions(req->plane, req->stride, req->qp, req->cuTreeOffsets, req->qgSize, req->output, *req->buffers);
        if (req->rowsReady)
            req->rowsReady->set(req->numActualRows);
        req->done.trigger();
        return;
    }

    enqueueMLRequest(this, req);
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

    if (req)
    {
        if (req->type == ML_PREPROCESS)
        {
            preprocessInput(req->plane, req->stride, *req->buffers);
            req->preprocessDone.trigger();
        }
        else if (req->type == ML_PREDICT_PREPARED)
        {
            predictPreparedPartitionsChunked(req->qp, req->cuTreeOffsets, req->qgSize, req->output, *req->buffers,
                                              req->rowsReady, req->numActualRows);
            req->done.trigger();
        }
        else
        {
            preprocessInput(req->plane, req->stride, *req->buffers);
            predictPreparedPartitionsChunked(req->qp, req->cuTreeOffsets, req->qgSize, req->output, *req->buffers,
                                              req->rowsReady, req->numActualRows);
            req->done.trigger();
        }
    }
}
} // namespace X265_NS

#endif // ENABLE_MLCTUPRED
