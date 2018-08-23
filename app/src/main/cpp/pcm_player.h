//
// Created by xiangtao on 2018/8/21.
//

#ifndef OPENSLES_PCM_PLAYER_H
#define OPENSLES_PCM_PLAYER_H

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <memory>
#include <vector>

class PcmPlayer {
public:
    PcmPlayer();

    ~PcmPlayer() {
        Release();
    }

    void Start();

    void Stop();

    void FeedPcmData(uint8_t *pcm, size_t size);

    void Init(SLuint32 channels,
              SLuint32 sampleRate,
              SLuint32 bitsPerSample);

    void Release();

private:
    struct PcmBufferPool;
    struct PcmBufferBlockingQueue;

    SLObjectItf engineObject;
    SLEngineItf engineEngine;
    SLObjectItf outputMixObject;
    SLObjectItf playerObject;
    SLPlayItf playerPlay;
    SLAndroidSimpleBufferQueueItf audioBufferQueue;

    std::shared_ptr<PcmBufferPool> pcmBufferPool;
    std::shared_ptr<PcmBufferBlockingQueue> pcmBufferBlockingQueue;

    typedef std::vector<uint8_t> PcmBuffer;
    PcmBuffer pcmBuffer;
    bool callBacked;

    friend void BufferQueueCallback(SLAndroidSimpleBufferQueueItf bufferQueue, void *pcmPlayer);
};

#endif //OPENSLES_PCM_PLAYER_H
