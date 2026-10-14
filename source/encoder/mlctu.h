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

#ifndef X265_MLCTU_H
#define X265_MLCTU_H

#ifdef ENABLE_MLCTUPRED

#include "common.h"
#include "threading.h"
#include "threadpool.h"
#if defined(__MINGW32__)
#include <specstrings.h>
#ifndef _Frees_ptr_opt_
#define _Frees_ptr_opt_ /* SAL annotation missing from MinGW, used by ONNX Runtime */
#endif
#endif
#include <onnxruntime_c_api.h>
#include <queue>

/* Model block size, independent of maxCUSize */
#define ML_BLOCK_SIZE  64

/* Split probabilities per 64x64 CTU, Z-scan order:
 *   [0]     64x64 -> 32x32
 *   [1..4]  32x32 -> 16x16
 *   [5..20] 16x16 -> 8x8
 * A 32x32 CTU holds one value, 32x32 -> 16x16. */
#define CTU_PRED_SIZE  21
#define NUM_MODELS     3

/* At or above the split threshold the split is forced, at or below a no-split
 * threshold it is skipped, in between RD decides. No-split is gated tighter
 * since a wrong veto costs more than an extra split trial. */
#define ML_SPLIT_CONFIDENT_THRESHOLD      0.5f
#define ML_NOSPLIT_CONFIDENT_THRESHOLD    0.2f
#define ML_NOSPLIT_CONFIDENT_THRESHOLD_16 0.35f

/* Written on inference failure; between all thresholds, so RD decides */
#define ML_PRED_NEUTRAL ((ML_SPLIT_CONFIDENT_THRESHOLD + ML_NOSPLIT_CONFIDENT_THRESHOLD_16) / 2)

/* Max intra-op and preprocessing threads per inference */
#define ML_MAX_INTRAOP_THREADS 8

/* 64x64 block rows per inference call */
#define ML_ROW_CHUNK_SIZE 1

/* Minimum ONNX Runtime C API version */
#define ML_ORT_API_VERSION 17

namespace X265_NS {
// private x265 namespace

/* Model inputs, one entry per 64x64 block */
struct MLCTUBuffers
{
    float* qp;   // [numBlocks]            normalized QP
    float* b1;   // [numBlocks][16][16]    4x4 means minus 64x64 mean
    float* b2;   // [numBlocks][32][32]    2x2 means minus 32x32 mean
    float* b3;   // [numBlocks][64][64]    pixels minus 16x16 mean

    MLCTUBuffers() : qp(NULL), b1(NULL), b2(NULL), b3(NULL) {}
    ~MLCTUBuffers() { destroy(); }

    bool init(int numBlocks)
    {
        qp = X265_MALLOC(float, numBlocks);
        b1 = X265_MALLOC(float, numBlocks * 16 * 16);
        b2 = X265_MALLOC(float, numBlocks * 32 * 32);
        b3 = X265_MALLOC(float, numBlocks * 64 * 64);
        if (!isAllocated())
        {
            destroy();
            return false;
        }
        return true;
    }

    bool isAllocated() const { return qp && b1 && b2 && b3; }

    void destroy()
    {
        X265_FREE(qp); qp = NULL;
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
    pixel*             plane;
    intptr_t           stride;
    int                qp;
    const double*      cuTreeOffsets;
    uint32_t           qgSize;
    float*             output;
    MLCTUBuffers*      buffers;
    MLPredictionRequestType type;
    bool               preprocessQueued;
    Event              preprocessDone;
    Event              done;

    ThreadSafeInteger* rowsReady;     // CTU rows with predictions ready
    int                numActualRows;

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

    explicit MLCTUPredictor(x265_param* param);
    ~MLCTUPredictor();

    bool init();

    /* Run on m_pool, or synchronously without one. enqueuePreprocessRequest()
     * returns false if there is no pool. */
    bool enqueuePreprocessRequest(MLPredictionRequest* req);
    void enqueuePreparedRequest(MLPredictionRequest* req);
    void enqueueRequest(MLPredictionRequest* req);

    void findJob(int workerThreadId);

protected:

    x265_param*       m_param;
    int               m_numBlocksW;
    int               m_numBlocksH;
    int               m_numBlocks;
    int               m_intraOpThreads;   // ONNX intra-op threads per inference
    int               m_prepThreads;      // OpenMP threads for preprocessing

    const OrtApi*     m_ort;
    OrtEnv*           m_env;
    OrtMemoryInfo*    m_memoryInfo;
    OrtSession*       m_sessions[NUM_MODELS];

    std::queue<MLPredictionRequest*> m_requestQueue;
    Lock              m_queueLock;

    bool resolveModelDir(char* dir, size_t size) const;
    OrtSession* loadModel(const char* path);
    bool checkStatus(OrtStatus* status, const char* what, int level = X265_LOG_ERROR) const;
    void release();

    void preprocessInput(const pixel* plane, intptr_t stride, MLCTUBuffers& buffers);
    void fillQpBuffer(int qp, const double* cuTreeOffsets, uint32_t qgSize, MLCTUBuffers& buffers);
    void runModel(OrtSession* session, MLCTUBuffers& buffers, float* output, int blockOffset, int batchCount);
    void processOutput(const float* level1, const float* level2, const float* level3, float* output, int blockOffset, int batchCount);
    void predictPreparedPartitionsChunked(int qp, const double* cuTreeOffsets, uint32_t qgSize, float* output,
                                          MLCTUBuffers& buffers, ThreadSafeInteger* rowsReady, int numActualRows);
    void enqueue(MLPredictionRequest* req);
};
}

#endif // ENABLE_MLCTUPRED
#endif // ifndef X265_MLCTU_H
