// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/island/island_audio.h"
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propidl.h>
#include <wctype.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <new>
#include <string>
#include <thread>
#include "host/log.h"

namespace island {

// IAudioMeterInformation (endpointvolume.h), spelled out: some MinGW headers only declare its name.
struct PeakMeter : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetPeakValue(float* peak) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMeteringChannelCount(UINT* count) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChannelsPeakValues(UINT32 count, float* peaks) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD* mask) = 0;
};

namespace {

// Core Audio's ids, spelled out: MinGW doesn't attach them to all of its declarations.
const CLSID kDeviceEnumeratorClass = {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
const IID kDeviceEnumerator = {0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
const IID kAudioClient = {0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}};
const IID kCaptureClient = {0xc8adbd64, 0xe71e, 0x48a0, {0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17}};
const IID kSessionManager2 = {0x77aa99a0, 0x1bd6, 0x484f, {0x8b, 0xc7, 0x2c, 0x65, 0x4c, 0x9a, 0x9b, 0x6f}};
const IID kSessionControl2 = {0xbfb7ff88, 0x7239, 0x4fc9, {0x8f, 0xa2, 0x07, 0xc9, 0x50, 0xbe, 0x9c, 0x6d}};
const IID kMeterInformation = {0xc02216f6, 0x8c67, 0x4b5b, {0x9d, 0x00, 0xd0, 0x08, 0xe7, 0x3e, 0x00, 0x64}};
const IID kCompletionHandler = {0x41d949ab, 0x9862, 0x444a, {0x80, 0xf6, 0xc2, 0x61, 0x33, 0x4d, 0xa5, 0xeb}};
const IID kAgileObject = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
const IID kUnknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kSubFormatPcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID kSubFormatFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

// Process loopback (Windows 10 2004 and later), declared here under our own names: older MinGW
// headers lack audioclientactivationparams.h, newer ones have it, and private names clash with
// neither. The layout is AUDIOCLIENT_ACTIVATION_PARAMS with its PROCESS_LOOPBACK_PARAMS.
const wchar_t kProcessLoopbackDevice[] = L"VAD\\Process_Loopback";   // VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK
const DWORD kProcessLoopback = 1;       // AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK
const DWORD kIncludeProcessTree = 0;    // PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE
struct LoopbackParams {
    DWORD activationType;
    DWORD targetProcessId;
    DWORD loopbackMode;
};
static_assert(sizeof(LoopbackParams) == 12, "AUDIOCLIENT_ACTIVATION_PARAMS is three 32-bit fields");

// Resolved at run time: older import libraries don't have it, and this way MacWin.exe gains no
// import (mmdevapi.dll is in the process anyway once Core Audio is used).
using ActivateAsyncFn = HRESULT(WINAPI*)(LPCWSTR, REFIID, PROPVARIANT*, IActivateAudioInterfaceCompletionHandler*,
                                         IActivateAudioInterfaceAsyncOperation**);
using PackageFamilyNameFn = LONG(WINAPI*)(HANDLE, UINT32*, PWSTR);
using AvSetTaskFn = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
using AvRevertFn = BOOL(WINAPI*)(HANDLE);

template <class Fn>
Fn Resolve(HMODULE module, const char* name) {
    return module ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(module, name))) : nullptr;
}

constexpr int kWindow = 1024;                     // samples per analysis (21 ms at 48 kHz)
constexpr size_t kRingSize = 4096;
constexpr float kBandEdges[Spectrum::kBands + 1] = {60, 150, 400, 1000, 2500, 8000};   // Hz
constexpr float kSilenceRms = 0.001f;             // -60 dBFS: below it the bars rest
constexpr float kGainFloorDb = -45.0f;            // the auto-gain never goes lower, so hiss stays at 0
constexpr float kRangeDb = 36.0f;                 // from the band's recent peak down to an empty bar
constexpr float kPeakFallDbPerSecond = 6.0f;
constexpr float kAttackSeconds = 0.010f;          // time constant: up within ~30 ms
constexpr float kReleaseSeconds = 0.085f;         // down within ~250 ms
constexpr auto kAnalyzeEvery = std::chrono::milliseconds(16);
constexpr auto kQuietAfter = std::chrono::milliseconds(150);   // no packets: nothing plays
constexpr auto kDeviceCheckEvery = std::chrono::seconds(2);
constexpr DWORD kRetryMs = 2000;
constexpr DWORD kActivateTimeoutMs = 5000;
constexpr REFERENCE_TIME kProcessBuffer = 200000;     // 20 ms, as in Microsoft's application loopback sample
constexpr REFERENCE_TIME kLoopbackBuffer = 1000000;   // 100 ms: polled, so leave room for a late wake-up

using Clock = std::chrono::steady_clock;

// The samples as they come from the audio engine.
struct Format {
    enum Kind { Float32, Int16, Int24, Int32 } kind = Float32;
    int channels = 0;
    int blockAlign = 0;
    int bytesPerSample = 0;
    int rate = 0;
};

bool ParseFormat(const WAVEFORMATEX* f, Format* out) {
    if (!f || f->nChannels == 0 || f->nSamplesPerSec == 0) return false;
    WORD tag = f->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        if (f->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) return false;
        const GUID& sub = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f)->SubFormat;
        if (IsEqualGUID(sub, kSubFormatFloat)) {
            tag = WAVE_FORMAT_IEEE_FLOAT;
        } else if (IsEqualGUID(sub, kSubFormatPcm)) {
            tag = WAVE_FORMAT_PCM;
        } else {
            return false;
        }
    }
    // A 24-bit sample in a 32-bit container is left-aligned, so the container size decides.
    Format r;
    if (tag == WAVE_FORMAT_IEEE_FLOAT && f->wBitsPerSample == 32) {
        r.kind = Format::Float32;
    } else if (tag == WAVE_FORMAT_PCM && f->wBitsPerSample == 16) {
        r.kind = Format::Int16;
    } else if (tag == WAVE_FORMAT_PCM && f->wBitsPerSample == 24) {
        r.kind = Format::Int24;
    } else if (tag == WAVE_FORMAT_PCM && f->wBitsPerSample == 32) {
        r.kind = Format::Int32;
    } else {
        return false;
    }
    r.channels = f->nChannels;
    r.bytesPerSample = f->wBitsPerSample / 8;
    r.blockAlign = f->nBlockAlign;
    r.rate = (int)f->nSamplesPerSec;
    if (r.blockAlign < r.channels * r.bytesPerSample) return false;
    *out = r;
    return true;
}

// The last few thousand samples, mixed down to mono.
class Ring {
public:
    void Clear() {
        buffer_.fill(0.0f);
        pos_ = 0;
    }
    void Push(float v) {
        buffer_[pos_] = v;
        pos_ = (pos_ + 1) % kRingSize;
    }
    void PushSilence(UINT32 frames) {
        for (UINT32 i = 0; i < frames && i < kRingSize; ++i) Push(0.0f);
    }
    void Push(const BYTE* data, UINT32 frames, const Format& f) {
        for (UINT32 i = 0; i < frames; ++i) {
            const BYTE* p = data + (size_t)i * f.blockAlign;
            float sum = 0.0f;
            for (int c = 0; c < f.channels; ++c, p += f.bytesPerSample) sum += Sample(p, f.kind);
            Push(sum / (float)f.channels);
        }
    }
    // The newest `n` samples, oldest first.
    void Last(float* out, size_t n) const {
        size_t start = (pos_ + kRingSize - n) % kRingSize;
        for (size_t i = 0; i < n; ++i) out[i] = buffer_[(start + i) % kRingSize];
    }

private:
    static float Sample(const BYTE* p, Format::Kind kind) {
        switch (kind) {
        case Format::Float32: {
            float v;
            memcpy(&v, p, sizeof(v));
            return std::isfinite(v) ? v : 0.0f;
        }
        case Format::Int16: {
            INT16 v;
            memcpy(&v, p, sizeof(v));
            return (float)v / 32768.0f;
        }
        case Format::Int24: {
            const INT32 v = (INT32)((UINT32)p[0] << 8 | (UINT32)p[1] << 16 | (UINT32)p[2] << 24);
            return (float)v / 2147483648.0f;
        }
        case Format::Int32: {
            INT32 v;
            memcpy(&v, p, sizeof(v));
            return (float)v / 2147483648.0f;
        }
        }
        return 0.0f;
    }

    std::array<float, kRingSize> buffer_ = {};
    size_t pos_ = 0;
};

// Samples in, five smoothed bar heights out.
class Analyzer {
public:
    Analyzer() {
        const double pi = 3.14159265358979323846;
        for (int i = 0; i < kWindow; ++i) window_[i] = (float)(0.5 - 0.5 * std::cos(2.0 * pi * i / (kWindow - 1)));
        for (int k = 0; k < kWindow / 2; ++k) {
            cos_[k] = (float)std::cos(2.0 * pi * k / kWindow);
            sin_[k] = (float)std::sin(2.0 * pi * k / kWindow);
        }
    }

    void Reset(int rate) {
        for (int b = 0; b < Spectrum::kBands; ++b) {
            const int top = kWindow / 2;
            lo_[b] = std::clamp((int)std::lround(kBandEdges[b] * kWindow / rate), 1, top - 1);
            hi_[b] = std::clamp((int)std::lround(kBandEdges[b + 1] * kWindow / rate), lo_[b] + 1, top);
            peak_[b] = kGainFloorDb;
            level_[b] = 0.0f;
        }
    }

    // `samples` (kWindow of them) is null while nothing plays: the bars fall back to rest.
    void Step(const float* samples, float dt, float out[Spectrum::kBands]) {
        float target[Spectrum::kBands] = {};
        for (float& p : peak_) p = std::max(p - kPeakFallDbPerSecond * dt, kGainFloorDb);
        if (samples && Rms(samples) >= kSilenceRms) {
            for (int i = 0; i < kWindow; ++i) {
                re_[i] = samples[i] * window_[i];
                im_[i] = 0.0f;
            }
            Fft();
            // A full-scale sine through the Hann window peaks at N/4: that is 0 dB.
            const float norm = 16.0f / ((float)kWindow * (float)kWindow);
            for (int b = 0; b < Spectrum::kBands; ++b) {
                float power = 0.0f;
                for (int k = lo_[b]; k < hi_[b]; ++k) power += re_[k] * re_[k] + im_[k] * im_[k];
                const float db = 10.0f * std::log10(power * norm + 1e-20f);
                peak_[b] = std::max(peak_[b], db);
                target[b] = std::clamp((db - (peak_[b] - kRangeDb)) / kRangeDb, 0.0f, 1.0f);
            }
        }
        const float attack = 1.0f - std::exp(-dt / kAttackSeconds);
        const float release = 1.0f - std::exp(-dt / kReleaseSeconds);
        for (int b = 0; b < Spectrum::kBands; ++b) {
            level_[b] += (target[b] - level_[b]) * (target[b] > level_[b] ? attack : release);
            out[b] = level_[b];
        }
    }

private:
    static float Rms(const float* s) {
        double sum = 0.0;
        for (int i = 0; i < kWindow; ++i) sum += (double)s[i] * s[i];
        return (float)std::sqrt(sum / kWindow);
    }

    // In-place iterative radix-2 FFT of re_ + i·im_.
    void Fft() {
        for (int i = 1, j = 0; i < kWindow; ++i) {
            int bit = kWindow >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) {
                std::swap(re_[i], re_[j]);
                std::swap(im_[i], im_[j]);
            }
        }
        for (int len = 2; len <= kWindow; len <<= 1) {
            const int half = len / 2, step = kWindow / len;
            for (int i = 0; i < kWindow; i += len) {
                for (int k = 0; k < half; ++k) {
                    const float wr = cos_[k * step], wi = -sin_[k * step];
                    const int a = i + k, b = a + half;
                    const float xr = re_[b] * wr - im_[b] * wi;
                    const float xi = re_[b] * wi + im_[b] * wr;
                    re_[b] = re_[a] - xr;
                    im_[b] = im_[a] - xi;
                    re_[a] += xr;
                    im_[a] += xi;
                }
            }
        }
    }

    std::array<float, kWindow> window_ = {}, re_ = {}, im_ = {};
    std::array<float, kWindow / 2> cos_ = {}, sin_ = {};
    int lo_[Spectrum::kBands] = {}, hi_[Spectrum::kBands] = {};
    float peak_[Spectrum::kBands] = {}, level_[Spectrum::kBands] = {};
};

// An open loopback stream.
struct Capture {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    ComPtr<IMMDeviceEnumerator> devices;   // whole output only: to notice a new default device
    std::wstring deviceId;
    HANDLE ready = nullptr;                // process loopback only: a packet is there
    Format format;

    Capture() = default;
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    ~Capture() { Reset(); }
    void Reset() {
        capture.reset();
        client.reset();
        devices.reset();
        deviceId.clear();
        if (ready) CloseHandle(ready);
        ready = nullptr;
    }
};

// ActivateAudioInterfaceAsync calls back on a thread of its own: it only signals.
class ActivationHandler final : public IActivateAudioInterfaceCompletionHandler {
public:
    ActivationHandler() : done_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    HANDLE Done() const { return done_; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        // Agile: the audio service may call from any apartment without marshalling.
        if (IsEqualIID(riid, kUnknown) || IsEqualIID(riid, kCompletionHandler) || IsEqualIID(riid, kAgileObject)) {
            *out = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --refs_;
        if (left == 0) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        SetEvent(done_);
        return S_OK;
    }

private:
    ~ActivationHandler() {
        if (done_) CloseHandle(done_);
    }

    std::atomic<ULONG> refs_{1};
    HANDLE done_;
};

// An IAudioClient that hears only `pid` and its children. Gives up early when `wake` is set
// (Stop() or another pid).
HRESULT ActivateProcessClient(DWORD pid, HANDLE wake, ComPtr<IAudioClient>* out) {
    static const ActivateAsyncFn activate = Resolve<ActivateAsyncFn>(
        LoadLibraryExW(L"mmdevapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), "ActivateAudioInterfaceAsync");
    if (!activate) return E_NOTIMPL;

    LoopbackParams params = {kProcessLoopback, pid, kIncludeProcessTree};
    PROPVARIANT pv;
    memset(&pv, 0, sizeof(pv));
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(params);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    ActivationHandler* handler = new (std::nothrow) ActivationHandler();
    if (!handler) return E_OUTOFMEMORY;
    if (!handler->Done()) {
        handler->Release();
        return E_FAIL;
    }
    ComPtr<IActivateAudioInterfaceAsyncOperation> op;
    HRESULT hr = activate(kProcessLoopbackDevice, kAudioClient, &pv, handler, op.put());
    if (SUCCEEDED(hr)) {
        const HANDLE waits[2] = {handler->Done(), wake};
        const DWORD r = WaitForMultipleObjects(2, waits, FALSE, kActivateTimeoutMs);
        if (r == WAIT_OBJECT_0 + 1) {
            hr = E_ABORT;
        } else if (r != WAIT_OBJECT_0) {
            hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
    }
    // The operation holds its own reference until it completes, so letting go here is safe even
    // when the wait gave up.
    handler->Release();
    if (FAILED(hr)) return hr;

    HRESULT result = E_FAIL;
    ComPtr<IUnknown> unknown;
    hr = op->GetActivateResult(&result, unknown.put());
    if (FAILED(hr)) return hr;
    if (FAILED(result)) return result;
    if (!unknown) return E_NOINTERFACE;
    return unknown->QueryInterface(kAudioClient, out->put_void());
}

WAVEFORMATEX MakeFormat(bool floating) {
    WAVEFORMATEX f = {};
    f.wFormatTag = floating ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
    f.nChannels = 2;
    f.nSamplesPerSec = floating ? 48000 : 44100;
    f.wBitsPerSample = floating ? 32 : 16;
    f.nBlockAlign = (WORD)(f.nChannels * f.wBitsPerSample / 8);
    f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
    return f;
}

// What one process (and its children) plays. It has no mix format of its own: the stream is
// opened in a format we pick and Windows converts to it.
HRESULT OpenProcessLoopback(DWORD pid, HANDLE wake, Capture* cap) {
    HRESULT hr = E_FAIL;
    for (bool floating : {true, false}) {
        // A client whose Initialize failed isn't reliably usable again: each try activates anew.
        ComPtr<IAudioClient> client;
        hr = ActivateProcessClient(pid, wake, &client);
        if (hr == E_ABORT || hr == E_NOTIMPL) return hr;
        if (FAILED(hr)) continue;
        WAVEFORMATEX f = MakeFormat(floating);
        const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, kProcessBuffer, 0, &f, nullptr);
        if (FAILED(hr)) continue;
        if (!ParseFormat(&f, &cap->format)) return E_UNEXPECTED;
        hr = client->GetService(kCaptureClient, cap->capture.put_void());
        if (FAILED(hr)) return hr;
        cap->ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!cap->ready) return HRESULT_FROM_WIN32(GetLastError());
        hr = client->SetEventHandle(cap->ready);
        if (FAILED(hr)) return hr;
        cap->client = client;
        return S_OK;
    }
    return hr;
}

HRESULT DefaultOutputId(IMMDeviceEnumerator* devices, std::wstring* id, ComPtr<IMMDevice>* device) {
    ComPtr<IMMDevice> d;
    HRESULT hr = devices->GetDefaultAudioEndpoint(eRender, eConsole, d.put());
    if (FAILED(hr)) return hr;
    LPWSTR raw = nullptr;
    hr = d->GetId(&raw);
    if (FAILED(hr)) return hr;
    *id = raw ? raw : L"";
    CoTaskMemFree(raw);
    if (device) *device = d;
    return S_OK;
}

// Everything the default output plays, in the engine's own format, polled.
HRESULT OpenDefaultLoopback(Capture* cap) {
    HRESULT hr = CoCreateInstance(kDeviceEnumeratorClass, nullptr, CLSCTX_ALL, kDeviceEnumerator,
                                  cap->devices.put_void());
    if (FAILED(hr)) return hr;
    ComPtr<IMMDevice> device;
    hr = DefaultOutputId(cap->devices.get(), &cap->deviceId, &device);
    if (FAILED(hr)) return hr;
    ComPtr<IAudioClient> client;
    hr = device->Activate(kAudioClient, CLSCTX_ALL, nullptr, client.put_void());
    if (FAILED(hr)) return hr;
    WAVEFORMATEX* mix = nullptr;
    hr = client->GetMixFormat(&mix);
    if (FAILED(hr)) return hr;
    if (!ParseFormat(mix, &cap->format)) {
        CoTaskMemFree(mix);
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, kLoopbackBuffer, 0, mix, nullptr);
    CoTaskMemFree(mix);
    if (FAILED(hr)) return hr;
    hr = client->GetService(kCaptureClient, cap->capture.put_void());
    if (FAILED(hr)) return hr;
    cap->client = client;
    return S_OK;
}

// What the capture thread shares with its Spectrum.
struct Link {
    HANDLE wake;
    std::atomic<bool>* stop;
    std::atomic<DWORD>* pid;
    std::atomic<bool>* valid;
    std::atomic<float>* bands;

    bool Moved(DWORD from) const { return stop->load() || pid->load() != from; }
    void Publish(const float* levels) const {
        for (int b = 0; b < Spectrum::kBands; ++b) bands[b].store(levels[b], std::memory_order_relaxed);
        valid->store(true, std::memory_order_release);
    }
    void Clear() const {
        valid->store(false, std::memory_order_release);
        for (int b = 0; b < Spectrum::kBands; ++b) bands[b].store(0.0f, std::memory_order_relaxed);
    }
};

// Reads and analyses until Stop(), another pid (S_OK), a new default output (S_FALSE: reopen
// at once) or an error.
HRESULT Pump(Capture& cap, DWORD pid, const Link& link, Analyzer& analyzer) {
    HRESULT hr = cap.client->Start();
    if (FAILED(hr)) return hr;
    Ring ring;
    std::array<float, kWindow> samples;
    float levels[Spectrum::kBands] = {};
    Clock::time_point now = Clock::now();
    Clock::time_point lastData = now - kQuietAfter, lastAnalysis = now, nextDeviceCheck = now + kDeviceCheckEvery;
    bool quiet = true;
    const HANDLE waits[2] = {link.wake, cap.ready};
    const DWORD count = cap.ready ? 2 : 1;
    for (;;) {
        const DWORD r = WaitForMultipleObjects(count, waits, FALSE, 10);
        if (r == WAIT_FAILED) return HRESULT_FROM_WIN32(GetLastError());
        if (link.Moved(pid)) return S_OK;

        for (;;) {
            UINT32 packet = 0;
            hr = cap.capture->GetNextPacketSize(&packet);
            if (FAILED(hr)) return hr;
            if (packet == 0) break;
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = cap.capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) return hr;
            if (hr == AUDCLNT_S_BUFFER_EMPTY) break;
            if (quiet) {
                // Sound again after a pause: the window shouldn't mix in what played before it.
                ring.Clear();
                quiet = false;
            }
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data) {
                ring.PushSilence(frames);
            } else {
                ring.Push(data, frames, cap.format);
            }
            hr = cap.capture->ReleaseBuffer(frames);
            if (FAILED(hr)) return hr;
            if (frames) lastData = Clock::now();
        }

        now = Clock::now();
        if (now - lastAnalysis >= kAnalyzeEvery) {
            const float dt = std::min(std::chrono::duration<float>(now - lastAnalysis).count(), 0.1f);
            lastAnalysis = now;
            // Neither loopback delivers packets while nothing plays: that is silence, not an error.
            quiet = now - lastData > kQuietAfter;
            if (!quiet) ring.Last(samples.data(), kWindow);
            analyzer.Step(quiet ? nullptr : samples.data(), dt, levels);
            link.Publish(levels);
        }

        // The whole-output stream stays on the device it was opened on: follow a new default.
        if (cap.devices && now >= nextDeviceCheck) {
            nextDeviceCheck = now + kDeviceCheckEvery;
            std::wstring id;
            if (FAILED(DefaultOutputId(cap.devices.get(), &id, nullptr)) || id != cap.deviceId) return S_FALSE;
        }
    }
}

// MMCSS lifts the capture thread above ordinary work so the bars don't stutter when the PC is
// busy. Optional: without avrt.dll the thread just runs at normal priority.
class Mmcss {
public:
    Mmcss() {
        avrt_ = LoadLibraryExW(L"avrt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (auto set = Resolve<AvSetTaskFn>(avrt_, "AvSetMmThreadCharacteristicsW")) {
            DWORD index = 0;
            task_ = set(L"Games", &index);
        }
    }
    ~Mmcss() {
        if (task_) {
            if (auto revert = Resolve<AvRevertFn>(avrt_, "AvRevertMmThreadCharacteristics")) revert(task_);
        }
        if (avrt_) FreeLibrary(avrt_);
    }
    Mmcss(const Mmcss&) = delete;
    Mmcss& operator=(const Mmcss&) = delete;

private:
    HMODULE avrt_ = nullptr;
    HANDLE task_ = nullptr;
};

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)towlower(c);
    return s;
}

// "C:\\Program Files\\Spotify\\Spotify.exe" -> "spotify"; the same for an id like "Spotify.exe".
std::wstring ExeStem(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name = Lower(slash == std::wstring::npos ? path : path.substr(slash + 1));
    if (name.size() > 4 && name.compare(name.size() - 4, 4, L".exe") == 0) name.resize(name.size() - 4);
    return name;
}

// Does process `pid` belong to the app: its package family, or else its exe's name.
bool ProcessMatches(DWORD pid, const std::wstring& family, const std::wstring& stem) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    bool match = false;
    if (!family.empty()) {
        static const PackageFamilyNameFn familyOf =
            Resolve<PackageFamilyNameFn>(GetModuleHandleW(L"kernel32.dll"), "GetPackageFamilyName");
        wchar_t name[128];
        UINT32 len = ARRAYSIZE(name);
        match = familyOf && familyOf(process, &len, name) == ERROR_SUCCESS && _wcsicmp(name, family.c_str()) == 0;
    } else {
        wchar_t path[MAX_PATH * 2];
        DWORD len = ARRAYSIZE(path);
        match = QueryFullProcessImageNameW(process, 0, path, &len) && ExeStem(path) == stem;
    }
    CloseHandle(process);
    return match;
}

}  // namespace

Spectrum::~Spectrum() { Stop(); }

void Spectrum::Start(DWORD pid) {
    if (thread_.joinable()) {
        if (pid_.exchange(pid) != pid) SetEvent(wake_);
        return;
    }
    if (!wake_) wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!wake_) return;
    stop_ = false;
    pid_ = pid;
    valid_ = false;
    thread_ = std::thread([this] { Run(); });
}

void Spectrum::Stop() {
    if (thread_.joinable()) {
        stop_ = true;
        SetEvent(wake_);
        thread_.join();
    }
    if (wake_) {
        CloseHandle(wake_);
        wake_ = nullptr;
    }
    valid_ = false;
    for (std::atomic<float>& b : bands_) b.store(0.0f);
}

bool Spectrum::Read(float out[kBands]) const {
    if (!valid_.load(std::memory_order_acquire)) return false;
    for (int b = 0; b < kBands; ++b) out[b] = bands_[b].load(std::memory_order_relaxed);
    return true;
}

void Spectrum::Run() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Mmcss mmcss;
    const Link link = {wake_, &stop_, &pid_, &valid_, bands_};
    // On the heap: a few pages of tables and buffers have no business on the thread's stack.
    Analyzer* analyzer = new (std::nothrow) Analyzer();
    HRESULT logged = S_OK;
    DWORD unsupportedPid = 0;
    while (analyzer && !stop_) {
        const DWORD pid = pid_.load();
        Capture cap;
        HRESULT hr = E_FAIL;
        bool whole = true;
        if (pid != 0) {
            hr = OpenProcessLoopback(pid, wake_, &cap);
            if (hr == E_ABORT || link.Moved(pid)) continue;
            whole = FAILED(hr);
            if (whole && unsupportedPid != pid) {
                unsupportedPid = pid;
                Log(L"island: can't listen to process %lu alone (0x%08lX), listening to the whole output", pid,
                    (unsigned long)hr);
            }
        }
        if (whole) {
            cap.Reset();
            hr = OpenDefaultLoopback(&cap);
        }
        if (link.Moved(pid)) continue;
        if (SUCCEEDED(hr)) {
            analyzer->Reset(cap.format.rate);
            hr = Pump(cap, pid, link, *analyzer);
            cap.client->Stop();
        }
        link.Clear();
        if (FAILED(hr)) {
            // Usually the output went away (unplugged headphones): wait for Windows to pick another.
            if (hr != logged) Log(L"island: sound level stream failed (0x%08lX), trying again", (unsigned long)hr);
            logged = hr;
            WaitForSingleObject(wake_, kRetryMs);
        } else {
            logged = S_OK;
        }
    }
    delete analyzer;
    link.Clear();
    if (SUCCEEDED(com)) CoUninitialize();
}

DWORD AudioProcessFor(const std::wstring& aumid) {
    if (aumid.empty()) return 0;
    // Packaged ids are "<package family>!<app>"; anything else names (or ends in) the exe.
    const size_t bang = aumid.find(L'!');
    const std::wstring family = bang == std::wstring::npos ? L"" : aumid.substr(0, bang);
    const std::wstring stem = family.empty() ? ExeStem(aumid) : L"";
    if (family.empty() && stem.empty()) return 0;

    // The caller's thread usually has COM already; this only covers one that doesn't.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD active = 0, other = 0;
    {
        ComPtr<IMMDeviceEnumerator> devices;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<IAudioSessionEnumerator> sessions;
        int count = 0;
        if (SUCCEEDED(CoCreateInstance(kDeviceEnumeratorClass, nullptr, CLSCTX_ALL, kDeviceEnumerator,
                                       devices.put_void())) &&
            SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, device.put())) &&
            SUCCEEDED(device->Activate(kSessionManager2, CLSCTX_ALL, nullptr, manager.put_void())) &&
            SUCCEEDED(manager->GetSessionEnumerator(sessions.put())) && SUCCEEDED(sessions->GetCount(&count))) {
            for (int i = 0; i < count && !active; ++i) {
                ComPtr<IAudioSessionControl> control;
                ComPtr<IAudioSessionControl2> control2;
                if (FAILED(sessions->GetSession(i, control.put())) || !control) continue;
                if (FAILED(control->QueryInterface(kSessionControl2, control2.put_void()))) continue;
                if (control2->IsSystemSoundsSession() == S_OK) continue;
                DWORD pid = 0;
                AudioSessionState state = AudioSessionStateInactive;
                // AUDCLNT_S_NO_SINGLE_PROCESS still names one of the processes, good enough here.
                if (FAILED(control2->GetProcessId(&pid)) || pid == 0) continue;
                if (FAILED(control->GetState(&state)) || state == AudioSessionStateExpired) continue;
                if (pid == active || pid == other || !ProcessMatches(pid, family, stem)) continue;
                if (state == AudioSessionStateActive) {
                    active = pid;
                } else if (!other) {
                    other = pid;
                }
            }
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return active ? active : other;
}

MicMeter::MicMeter() = default;

MicMeter::~MicMeter() { Stop(); }

bool MicMeter::Start() {
    if (meter_) return true;
    ComPtr<IMMDeviceEnumerator> devices;
    ComPtr<IMMDevice> device;
    if (FAILED(CoCreateInstance(kDeviceEnumeratorClass, nullptr, CLSCTX_ALL, kDeviceEnumerator, devices.put_void())))
        return false;
    if (FAILED(devices->GetDefaultAudioEndpoint(eCapture, eCommunications, device.put()))) return false;
    if (FAILED(device->Activate(kMeterInformation, CLSCTX_ALL, nullptr, meter_.put_void()))) {
        meter_.reset();
        return false;
    }
    return true;
}

void MicMeter::Stop() { meter_.reset(); }

float MicMeter::Peak() const {
    if (!meter_) return 0.0f;
    float peak = 0.0f;
    if (FAILED(meter_->GetPeakValue(&peak))) return 0.0f;
    return std::clamp(peak, 0.0f, 1.0f);
}

}  // namespace island
