// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>
#include "host/com.h"
#include "host/render/genie_effect.h"
#include "host/snapshot_cache.h"

// One accepted animation request, filled in by the IPC thread (and the taskbar locator)
// and handed to the render thread.
struct GenieJob {
    HWND hwnd = nullptr;
    bool rising = false;

    // Snapshot: a fresh capture (raw texture + crop rect in it) or the cached one.
    bool useCache = false;
    ComPtr<ID3D11Texture2D> raw;
    int cropX = 0, cropY = 0, cropW = 0, cropH = 0;
    bool forceOpaque = false;

    RECT winRect = {};          // DWM extended frame, physical px
    HMONITOR hMon = nullptr;    // monitor whose taskbar the genie targets
    float cornerRadiusPx = 0;   // physical px, 0 for maximized windows
    float cornerSmoothing = 0;  // 0..1, Figma-style (see CornerShape)
    int style = kGenieModern;
    GenieParams params = {};
    int durationMs = 450;
    int tiles = 48;

    // Duplicated from the requesting DLL; owned by the job.
    HANDLE hFirst = nullptr;
    HANDLE hDone = nullptr;
    HANDLE hRevealed = nullptr;
    LARGE_INTEGER requestedAt = {};
};

class Renderer {
public:
    bool Start();
    void Stop();

    // Free-threaded device for snapshot uploads from other threads.
    ComPtr<ID3D11Device> Device();

    // Accepted jobs per window (queued, waiting for a taskbar lookup, or animating).
    // A request for an active window never needs a new snapshot or a fresh start: it
    // joins or turns around the genie that is already running (see Retarget).
    void Retain(HWND hwnd);
    bool IsActive(HWND hwnd);
    bool HasSnapshot(HWND hwnd, int w, int h) { return cache_.Has(hwnd, w, h); }
    void Submit(GenieJob* job);   // takes ownership of a retained job
    void Evict(HWND hwnd) { cache_.Erase(hwnd); }
    // Finishes a retained job that won't be animated: the DLL is told to finish now.
    void FailJob(GenieJob* job);

private:
    struct Slot {
        HWND hwnd = nullptr;
        ComPtr<IDXGISwapChain1> swap;
        ComPtr<IDCompositionTarget> target;
        ComPtr<IDCompositionVisual> visual;
        ComPtr<ID3D11Texture2D> msaa;
        ComPtr<ID3D11RenderTargetView> rtv;
        int allocW = 0, allocH = 0;
        bool busy = false;
    };
    // One genie per window. Its direction can flip any number of times; the jobs of
    // the current direction are its waiters and are finished when it ends.
    struct Anim {
        HWND hwnd = nullptr;
        bool rising = false;
        int durationMs = 450;
        int tiles = 48;
        float cornerRadiusPx = 0;
        float cornerSmoothing = 0;
        GenieParams params = {};
        SnapshotCache::Entry snap;
        Slot* slot = nullptr;
        // Z-order: right below the taskbar the genie goes to, so the taskbar and its
        // icons stay on top and the window seems to slip into its icon.
        HWND insertAfter = HWND_TOPMOST;
        std::vector<GenieJob*> waiters;
        LARGE_INTEGER start = {};
        LARGE_INTEGER doneAt = {};
        LARGE_INTEGER requestedAt = {};
        enum Phase { kRunning, kLinger, kRetire } phase = kRunning;
        bool shown = false;
        bool firstPending = false;
        int frames = 0;
        int reversals = 0;
        double cpuMs = 0;
        double firstFrameMs = -1;
    };
    // A job whose direction was overridden. A superseded restore is released only once
    // the window is minimized again, so its DLL never reveals the window too early.
    struct Superseded {
        GenieJob* job;
        bool wasRising;
        LARGE_INTEGER since;
    };

    void ThreadMain(HANDLE ready, bool* ok);
    // The GPU device: hardware first, WARP (the CPU) if any step fails on it.
    bool CreateDevice();
    bool CreateDeviceOn(D3D_DRIVER_TYPE type);
    void DestroyDevice();
    // Device removed (driver update, TDR, GPU unplugged): every animation is finished
    // (the DLLs reveal their windows) and the device is made again.
    bool DeviceLost();
    void RecoverDevice();
    bool CompileShaders();
    ID3D11Buffer* GridIndices(int tiles, UINT* count);
    bool PrepareSnapshot(GenieJob* job, SnapshotCache::Entry* out);
    Slot* AcquireSlot(int w, int h);
    bool SetupSlot(Slot* s);
    bool CreateSlotTargets(Slot* s);
    Anim* FindAnim(HWND hwnd);
    void StartJob(GenieJob* job, LARGE_INTEGER now);
    void Retarget(Anim* a, GenieJob* job, LARGE_INTEGER now);
    void StepAnim(Anim* a, LARGE_INTEGER now);
    bool Draw(Anim* a, float t);
    void RetireAnim(Anim* a);
    void ProcessSuperseded(LARGE_INTEGER now, bool force);
    void FinishWaiter(GenieJob* job);
    void ReleaseJob(GenieJob* job);
    void AbortAll();
    void PumpMessages();
    bool WaitForTick();
    double Ms(LONGLONG ticks) const { return (double)ticks * 1000.0 / (double)qpcFreq_.QuadPart; }
    LONGLONG Ticks(double ms) const { return (LONGLONG)(ms * (double)qpcFreq_.QuadPart / 1000.0); }

    std::thread thread_;
    HANDLE wake_ = nullptr;
    volatile bool stop_ = false;

    SRWLOCK queueLock_ = SRWLOCK_INIT;
    std::vector<GenieJob*> queue_;

    SRWLOCK activeLock_ = SRWLOCK_INIT;
    std::unordered_map<HWND, int> active_;

    SRWLOCK deviceLock_ = SRWLOCK_INIT;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGIFactory2> factory_;
    ComPtr<IDCompositionDevice> dcomp_;
    ComPtr<ID3D11VertexShader> genieVS_, classicVS_, fullscreenVS_;
    ComPtr<ID3D11PixelShader> geniePS_, prepPS_;
    ComPtr<ID3D11Buffer> genieCB_, prepCB_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11RasterizerState> raster_;
    std::unordered_map<int, ComPtr<ID3D11Buffer>> grids_;
    UINT msaaCount_ = 1;
    bool warp_ = false;
    bool broken_ = false;   // no device could be made again: every job fails over to Windows

    typedef DWORD(WINAPI* WaitClock_t)(UINT, const HANDLE*, DWORD);
    WaitClock_t waitClock_ = nullptr;
    HMODULE dcompDll_ = nullptr;

    std::vector<std::unique_ptr<Slot>> slots_;
    std::vector<std::unique_ptr<Anim>> anims_;
    std::vector<Superseded> superseded_;
    SnapshotCache cache_;
    LARGE_INTEGER qpcFreq_ = {};
};
