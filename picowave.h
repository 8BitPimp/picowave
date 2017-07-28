#pragma once
#include <stdint.h>

namespace PicoWave {

enum {
    PW_OK,
    PW_ALREADY_OPEN,
    PW_WAVEINFO_ERROR,
    PW_THREAD_ABORT,
    PW_WAVEOUTOPEN_ERROR,
    PW_CREATETHREAD_ERROR,
    PW_CREATEEVENT_ERROR,
    PW_WAVEOUTCLOSE_ERROR,
    PW_WAVEOUTWRITE_ERROR,
    PW_WAVEOUTPREPHDR_ERROR,
    PW_CLOSEHANDLE_ERROR,
};

typedef void (*WaveProc)(
    void* buffer,           // audio buffer data pointer
    size_t bufferSize,      // audio buffer size in bytes
    void* user              // opaque user data pointer
);

struct WaveInfo {
    uint32_t sampleRate;    // sample rate in hz  (44100, 22050, ...)
    uint32_t bitDepth;      // bit depth in bits  (16, 8)
    uint32_t channels;      // number of channels (2, 1)
    uint32_t bufferSize;    // audio buffer size in bytes
    WaveProc callback;      // audio rendering callback function
    void* callbackData;     // user data passed to callback
};

struct WaveOut {

    WaveOut();
    ~WaveOut();

    bool open(const WaveInfo& info);

    bool start();

    bool pause();

    bool close();

    uint32_t lastError() const;

protected:
    struct Detail* _detail;
};

} // namespace Wave
