//
// Created by xiangtao on 2018/8/21.
//

#include "pcm_player.h"

#include <list>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <cassert>

struct PcmPlayer::PcmBufferBlockingQueue {
    void Enqueue(const PcmBuffer &pcmBuffer, size_t maxSize) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        queue_cond.wait(lock, [this, maxSize] { return queue.size() < maxSize; });
        queue.push_back(std::move(pcmBuffer));
        queue_cond.notify_one();
    }

    PcmBuffer Dequeue() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        queue_cond.wait(lock, [this] { return queue.size() > 0; });
        auto buffer = queue.front();
        queue.pop_front();
        queue_cond.notify_one();
        return buffer;
    }

private:
    std::mutex queue_mutex;
    std::condition_variable queue_cond;
    std::list<PcmBuffer> queue;
};

struct PcmPlayer::PcmBufferPool {
    PcmBuffer Get(size_t size) {
        if (pool.empty()) {
            return PcmBuffer(size);
        } else {
            auto buffer = pool.front();
            pool.pop_front();
            return buffer;
        }
    }

    void Return(const PcmBuffer &buffer) {
        pool.push_back(std::move(buffer));
    }

private:
    std::list<PcmBuffer> pool;
};

PcmPlayer::PcmPlayer() : engineObject(nullptr),
                         engineEngine(nullptr),
                         outputMixObject(nullptr),
                         playerObject(nullptr),
                         playerPlay(nullptr),
                         audioBufferQueue(nullptr),
                         callBacked(false) {
}

void BufferQueueCallback(SLAndroidSimpleBufferQueueItf bufferQueue, void *context) {
    PcmPlayer *pcmPlayer = (PcmPlayer *) context;

    PcmPlayer::PcmBuffer buffer = pcmPlayer->pcmBufferBlockingQueue->Dequeue();
    if (pcmPlayer->pcmBuffer.capacity() < buffer.size()) {
        pcmPlayer->pcmBuffer.resize(buffer.size());
    }
    memcpy(pcmPlayer->pcmBuffer.data(), buffer.data(), buffer.size());
    (*bufferQueue)->Enqueue(bufferQueue, pcmPlayer->pcmBuffer.data(),
                            static_cast<SLuint32>(buffer.size()));

    pcmPlayer->pcmBufferPool->Return(buffer);
    pcmPlayer->callBacked = true;
}

void PcmPlayer::Init(SLuint32 channels,
                     SLuint32 sampleRate,
                     SLuint32 bitsPerSample) {
    SLresult result;
    // init engine
    result = slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr);
    assert(result == SL_RESULT_SUCCESS);
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    assert(result == SL_RESULT_SUCCESS);
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
    assert(result == SL_RESULT_SUCCESS);

    //init output mix
    result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0, 0,
                                              0);
    assert(result == SL_RESULT_SUCCESS);
    result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    assert(result == SL_RESULT_SUCCESS);

    //init player
    SLDataLocator_OutputMix outputMix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
    SLDataLocator_AndroidSimpleBufferQueue dataSourceQueue = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
            2};

    SLDataFormat_PCM dataSourceFormat = {
            SL_DATAFORMAT_PCM,
            channels,
            sampleRate * 1000,
            bitsPerSample,
            bitsPerSample,
            channels == 1 ? SL_SPEAKER_FRONT_CENTER
                          : SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
            SL_BYTEORDER_LITTLEENDIAN
    };
    SLDataSource dataSource = {&dataSourceQueue, &dataSourceFormat};
    SLDataSink dataSink = {&outputMix, NULL};
    const SLInterfaceID ids[] = {SL_IID_BUFFERQUEUE};
    const SLboolean reqs[] = {SL_BOOLEAN_TRUE};

    result = (*engineEngine)->CreateAudioPlayer(engineEngine, &playerObject, &dataSource, &dataSink,
                                                1, ids,
                                                reqs);
    assert(result == SL_RESULT_SUCCESS);
    result = (*playerObject)->Realize(playerObject, SL_BOOLEAN_FALSE);
    assert(result == SL_RESULT_SUCCESS);

    result = (*playerObject)->GetInterface(playerObject, SL_IID_PLAY, &playerPlay);
    assert(result == SL_RESULT_SUCCESS);

    result = (*playerObject)->GetInterface(playerObject, SL_IID_BUFFERQUEUE, &audioBufferQueue);
    assert(result == SL_RESULT_SUCCESS);
    result = (*audioBufferQueue)->RegisterCallback(audioBufferQueue, BufferQueueCallback, this);
    assert(result == SL_RESULT_SUCCESS);

    pcmBufferPool = std::shared_ptr<PcmBufferPool>(new PcmBufferPool);
    pcmBufferBlockingQueue = std::shared_ptr<PcmBufferBlockingQueue>(new PcmBufferBlockingQueue);
}

void PcmPlayer::Start() {
    if (playerPlay != nullptr) {
        (*playerPlay)->SetPlayState(playerPlay, SL_PLAYSTATE_PLAYING);
    }
}

void PcmPlayer::Stop() {
    if (playerPlay != nullptr) {
        (*playerPlay)->SetPlayState(playerPlay, SL_PLAYSTATE_STOPPED);
    }
}

void PcmPlayer::FeedPcmData(uint8_t *pcm, size_t size) {
    PcmBuffer buffer = pcmBufferPool->Get(size);
    buffer.resize(size);
    memcpy(buffer.data(), pcm, size);
    pcmBufferBlockingQueue->Enqueue(buffer, 5);
    if (!callBacked) {
        SLuint32 playState;
        (*playerPlay)->GetPlayState(playerPlay, &playState);
        if (playState != SL_PLAYSTATE_PLAYING) {
            Start();
        }
        BufferQueueCallback(audioBufferQueue, this);
    }
}

void PcmPlayer::Release() {
    if (playerObject != nullptr) {
        (*playerObject)->Destroy(playerObject);
        playerObject = nullptr;
        playerPlay = nullptr;
        audioBufferQueue = nullptr;
    }

    if (outputMixObject != nullptr) {
        (*outputMixObject)->Destroy(outputMixObject);
        outputMixObject = nullptr;
    }

    if (engineObject != nullptr) {
        (*engineObject)->Destroy(engineObject);
        engineObject = nullptr;
        engineEngine = nullptr;
    }

    pcmBufferBlockingQueue = nullptr;
    pcmBufferPool = nullptr;
    callBacked = false;
}