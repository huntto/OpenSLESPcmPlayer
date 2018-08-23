#include <jni.h>
#include <string>
#include <thread>
#include <android/log.h>
#include <android/asset_manager_jni.h>
#include <cassert>

#include "pcm_player.h"
#include "android_fopen.h"

#define LOG_TAG "PcmPlayer"
#define LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, fmt, ##__VA_ARGS__);


static FILE *pcmFile;
static bool running;
static std::thread readThread;
static PcmPlayer pcmPlayer;

const size_t kBufferSize = 1024 * 8;
static std::vector<uint8_t> buffer(kBufferSize);

static void ReadPcmLoop() {
    while (running && !feof(pcmFile)) {
        fread(buffer.data(), buffer.size(), 1, pcmFile);
        pcmPlayer.FeedPcmData(buffer.data(), buffer.size());
    }

    pcmPlayer.Stop();
    pcmPlayer.Release();
}


extern "C" JNIEXPORT void JNICALL
Java_me_huntto_openslespcmplayer_MainActivity_stop(JNIEnv * /* env */,
                                                   jobject /* object */) {
    running = false;
    if (readThread.joinable()) {
        readThread.join();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_me_huntto_openslespcmplayer_MainActivity_start(JNIEnv *env,
                                                    jobject /* object */,
                                                    jobject assetMgr,
                                                    jstring filename) {
    android_fopen_set_asset_manager(AAssetManager_fromJava(env, assetMgr));
    // convert Java string to UTF-8
    const char *utf8 = env->GetStringUTFChars(filename, NULL);
    assert(NULL != utf8);

    // open the file to play
    pcmFile = android_fopen(utf8, "rb");
    if (pcmFile == NULL) {
        LOGE("Can not open file:%s", utf8);
        return;
    }

    pcmPlayer.Init(2, 44100, 16);
    running = true;
    readThread = std::thread(ReadPcmLoop);
}
