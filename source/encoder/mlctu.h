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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *****************************************************************************/

#ifndef X265_MLCTU_H
#define X265_MLCTU_H

#ifdef ENABLE_MLCTUPRED

#include "common.h"
#include <onnxruntime_cxx_api.h>
#include "threadpool.h"
#include <queue>

/* Per-CTU prediction layout (64x64 CTU, 3 levels):
 *   index 0       : split 64x64 into 32x32? (1 value)
 *   index 1..4    : split each 32x32 into 16x16? (4 values, Z-scan quadrant order)
 *   index 5..20   : split each 16x16 into 8x8?  (16 values, Z-scan order)
 */
#define CTU_PRED_SIZE  21
#define NUM_MODELS 3
/* Cap on intra-op/preprocessing threads per inference; benchmarked sweet spot
 * on many-core boxes. Effective width is derived in mlIntraOpThreads(). */
#define ML_MAX_INTRAOP_THREADS 8

/* Number of 64x64-block rows batched per ort->Run() call in
 * predictPreparedPartitionsChunked() */
#ifndef ML_ROW_CHUNK_SIZE
#define ML_ROW_CHUNK_SIZE 1
#endif

/* Per-frame std::chrono + printf profiling of preprocess/inference/post stages.
 * MUST stay 0 for production builds; set to 1 only when profiling. */
#ifndef PROFILE_ML
#define PROFILE_ML 0
#endif

#ifndef USE_QUANTIZED_MODEL
#define USE_QUANTIZED_MODEL 1
#endif

#ifndef ENABLE_ORT_PROFILE
#define ENABLE_ORT_PROFILE 0
#endif

#if PROFILE_ML
#include <chrono>
#endif

namespace X265_NS
{

typedef struct {
    const OrtApi* ort;           // ONNX Runtime API
    OrtEnv* env;                 // ONNX Runtime environment
    OrtSession* session;         // ONNX Runtime session
    OrtSessionOptions* session_options; // Session options
    OrtMemoryInfo* memory_info;  // Memory info for tensors
} CTUPartitionInference;

struct MLCTUBuffers
{
    float* b1;
    float* b2;
    float* b3;
    float* qp_buffer;

    MLCTUBuffers()
    {
        b1 = NULL;
        b2 = NULL;
        b3 = NULL;
        qp_buffer = NULL;
    }

    ~MLCTUBuffers()
    {
        destroy();
    }

    bool init(int totalCTUs, int /*maxCUSize*/)
    {
        qp_buffer = X265_MALLOC(float, totalCTUs);
        b1 = X265_MALLOC(float, totalCTUs * 256);
        b2 = X265_MALLOC(float, totalCTUs * 1024);
        b3 = X265_MALLOC(float, totalCTUs * 4096);

        return qp_buffer && b1 && b2 && b3;
    }

    void destroy()
    {
        X265_FREE(qp_buffer); qp_buffer = NULL;
        X265_FREE(b1); b1 = NULL;
        X265_FREE(b2); b2 = NULL;
        X265_FREE(b3); b3 = NULL;
    }
};

enum MLPredictionRequestType
{
    ML_PREPROCESS,
    ML_PREDICT_PREPARED,
    ML_PREDICT_FULL
};

struct MLPredictionRequest
{
    pixel* plane;
    intptr_t stride;
    int qp;
    const double* cuTreeOffsets;
    uint32_t qgSize;
    float* output;
    MLCTUBuffers* buffers;
    MLPredictionRequestType type;
    bool preprocessQueued;
    Event preprocessDone;
    Event done;

    ThreadSafeInteger* rowsReady;
    int numActualRows;

    MLPredictionRequest()
        : plane(NULL)
        , stride(0)
        , qp(0)
        , cuTreeOffsets(NULL)
        , qgSize(16)
        , output(NULL)
        , buffers(NULL)
        , type(ML_PREDICT_FULL)
        , preprocessQueued(false)
        , rowsReady(NULL)
        , numActualRows(0)
    {}
};

class MLCTUPredictor : public JobProvider
{
public:
    x265_param* m_param;
    int maxCUSize;
    int totalCTUs;
    /* ML thread budget (sized to core count in init()): ONNX intra-op threads
     * per session, and OMP threads for preprocessing. */
    int m_intraOpThreads;
    int m_prepThreads;
    OrtEnv*           m_env;
    CTUPartitionInference* m_sessions[NUM_MODELS];

    // Threading / queue variables
    std::queue<MLPredictionRequest*> m_requestQueue;
    Lock                             m_queueLock;

    explicit MLCTUPredictor(x265_param* param);
    ~MLCTUPredictor();
    bool init();
    CTUPartitionInference* init_ctu_onnx(const char*);
    void preprocessInput(pixel* plane, intptr_t stride, MLCTUBuffers& buffers);
    void fillQpBuffer(int QP, const double* cuTreeOffsets, uint32_t qgSize, MLCTUBuffers& buffers);
    void run_model(CTUPartitionInference* ctu, MLCTUBuffers& buffers, float* output, int ctuOffset, int batchCount);
    void process_output(const float* level_1, const float* level_2, const float* level_3, float* output, int ctuOffset, int batchCount);
    void predictPreparedPartitions(int QP, const double* cuTreeOffsets, uint32_t qgSize, float* output, MLCTUBuffers& buffers);
    void predictPartitions(pixel* plane, intptr_t stride, int QP, const double* cuTreeOffsets, uint32_t qgSize, float* output, MLCTUBuffers& buffers);
    void predictPreparedPartitionsChunked(int QP, const double* cuTreeOffsets, uint32_t qgSize, float* output,
                                          MLCTUBuffers& buffers, ThreadSafeInteger* rowsReady, int numActualRows);
    void cleanup_ctu_onnx();

    // JobProvider virtual method
    void findJob(int workerThreadId);

    // Add request and trigger prediction
    bool enqueuePreprocessRequest(MLPredictionRequest* req);
    void enqueuePreparedRequest(MLPredictionRequest* req);
    void enqueueRequest(MLPredictionRequest* req);
};

} // namespace X265_NS

#endif // ENABLE_MLCTUPRED
#endif // X265_MLCTU_H
