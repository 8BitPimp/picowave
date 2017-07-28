#include <array>
#include <cassert>
#include <cstring>

#include <Windows.h>

#include "picowave.h"

#define MMOK(EXP) ((EXP) == MMSYSERR_NOERROR)

namespace PicoWave {

// ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- WaveOut Implementation

struct Detail {

    Detail()
        : _hwo(NULL)
        , _alive(0)
        , _waveEvent(NULL)
        , _waveThread(NULL)
    {
        memset(&_wavehdr, 0, sizeof(_wavehdr));
        memset(&_info, 0, sizeof(_info));
    }

    ~Detail()
    {
        close();
    }

    bool open(const WaveInfo& info);

    bool start();

    bool pause();

    bool close();

protected:
    bool _prepare();

    static DWORD WINAPI _threadProc(LPVOID param);

    // internal wave info
    std::array<WAVEHDR, 4> _wavehdr;
    HWAVEOUT _hwo;
    LONG volatile _alive;
    HANDLE _waveEvent;
    HANDLE _waveThread;

    // todo:
    // - allocate single large audio buffer to slice into WAVEHDR
    // - align single large buffer when placed into WAVEHDR (128/256 bits?)
    // - document alignment support
    // - user supplied alignment ?

    // user supplied info
    WaveInfo _info;
};

bool Detail::_prepare()
{
    assert(_hwo);
    const size_t numSamples = _info.bufferSize * _info.channels;
    const size_t numBytes = numSamples * _info.bitDepth / 8;
    for (WAVEHDR& hdr : _wavehdr) {
        // allocate the wave header object
        memset(&hdr, 0, sizeof(hdr));
        hdr.lpData = (LPSTR) new int16_t[numBytes];
        hdr.dwBufferLength = numBytes;
        memset(hdr.lpData, 0, numBytes);
        // prepare the header for the device
        if (!MMOK(waveOutPrepareHeader(_hwo, &hdr, sizeof(hdr)))) {
            return false;
        }
        // write the buffer to the device
        if (!MMOK(waveOutWrite(_hwo, &hdr, sizeof(hdr)))) {
            return false;
        }
    }
    return true;
}

DWORD WINAPI Detail::_threadProc(LPVOID param)
{
    assert(param);
    Detail& self = *(Detail*)param;
    assert(self._hwo);
    // loop while this thread is alive
    while (self._alive) {
        // wait for a wave event
        const DWORD ret = WaitForSingleObject(self._waveEvent, INFINITE);
        // poll waveheaders for a free block
        for (WAVEHDR& hdr : self._wavehdr) {
            if ((hdr.dwFlags & WHDR_DONE) == 0) {
                // buffer is not free for use
                continue;
            }
            if (!MMOK(waveOutUnprepareHeader(self._hwo, &hdr, sizeof(hdr)))) {
                return 1;
            }
            // call user function to fill with new self
            WaveProc callback = self._info.callback;
            if (callback) {
                void* cbData = self._info.callbackData;
                callback(hdr.lpData, hdr.dwBufferLength, cbData);
            }
            if (!MMOK(waveOutPrepareHeader(self._hwo, &hdr, sizeof(hdr)))) {
                return 1;
            }
            if (!MMOK(waveOutWrite(self._hwo, &hdr, sizeof(WAVEHDR)))) {
                return 1;
            }
        }
    }
    return 0;
}

bool Detail::open(const WaveInfo& info)
{
    // check if already running
    if (_hwo || _waveThread || _waveEvent) {
        return false;
    }
    // mark callback thread as alive
    InterlockedExchange(&_alive, 1);
    // copy wave info structure to internal data for reference
    _info = info;
    // create waitable wave event
    _waveEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (_waveEvent == NULL) {
        return false;
    }
    // prepare output wave format
    WAVEFORMATEX waveformat;
    memset(&waveformat, 0, sizeof(waveformat));
    waveformat.cbSize = 0;
    waveformat.wFormatTag = WAVE_FORMAT_PCM;
    waveformat.nChannels = info.channels;
    waveformat.nSamplesPerSec = info.sampleRate;
    waveformat.wBitsPerSample = info.bitDepth;
    waveformat.nBlockAlign = (info.channels * waveformat.wBitsPerSample) / 8;
    waveformat.nAvgBytesPerSec = info.sampleRate * waveformat.nBlockAlign;
    // create wave output
    memset(&_hwo, 0, sizeof(_hwo));
    if (!MMOK(waveOutOpen(
            &_hwo,
            WAVE_MAPPER,
            &waveformat,
            (DWORD_PTR)_waveEvent,
            NULL,
            CALLBACK_EVENT))) {
        return false;
    }
    // create the wave thread
    _waveThread = CreateThread(
        NULL, 0, _threadProc, this, CREATE_SUSPENDED, 0);
    if (_waveThread == NULL) {
        return false;
    }
    // prepare waveout for playback
    return _prepare();
}

bool Detail::close()
{
    InterlockedExchange(&_alive, 0);
    if (_waveThread) {
        bool hardKill = true;
        const DWORD timeout = 1000;
        if (WaitForSingleObject(_waveThread, timeout)) {
            // thread join failed :'(
        }
        DWORD exitCode = 0;
        if (GetExitCodeThread(_waveThread, &exitCode)) {
            if (exitCode != STILL_ACTIVE) {
                hardKill = false;
            }
        }
        if (hardKill) {
            // note: hard kills are not good as they could leave the client process
            //       in an inconsistent state
            if (TerminateThread(_waveThread, 0) == FALSE) {
                // XXX: how to handle a thread that wont die?
            }
        }
        _waveThread = NULL;
    }
    if (_hwo) {
        if (!MMOK(waveOutClose(_hwo))) {
            return false;
        }
        _hwo = NULL;
    }
    if (_waveEvent) {
        CloseHandle(_waveEvent);
        _waveEvent = NULL;
    }
    memset(&_wavehdr, 0, sizeof(_wavehdr));
    memset(&_info, 0, sizeof(_info));
    return true;
}

bool Detail::start()
{
    if (!_waveThread) {
        return false;
    }
    ResumeThread(_waveThread);
    return true;
}

bool Detail::pause()
{
    if (!_waveThread) {
        return false;
    }
    SuspendThread(_waveThread);
    return true;
}

// ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- WaveOut Facade

WaveOut::WaveOut()
    : _detail(new Detail)
{
    assert(_detail);
}

WaveOut::~WaveOut()
{
    assert(_detail);
    delete _detail;
}

bool WaveOut::open(const WaveInfo& info)
{
    assert(_detail);
    return _detail->open(info);
}

bool WaveOut::start()
{
    assert(_detail);
    return _detail->start();
}

bool WaveOut::pause()
{
    assert(_detail);
    return _detail->pause();
}

bool WaveOut::close()
{
    assert(_detail);
    return _detail->close();
}

} // namespace PicoWave
