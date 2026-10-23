// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

#include "host/render/device.h"
#include <d3dcompiler.h>
#include <dwmapi.h>
#include <float.h>
#include <math.h>
#include "common/corner_shape.h"
#include "common/names.h"
#include "genie_hlsl.h"
#include "host/log.h"

namespace {

struct alignas(16) GenieConstants {
    float win[4];
    float icon[4];
    float canvas[4];
    float params[4];
    float tex[4];
};

struct alignas(16) PrepConstants {
    int prep[4];
};

LRESULT CALLBACK GhostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int RoundUp(int v, int step) {
    return ((v + step - 1) / step) * step;
}

void SignalAndClose(HANDLE* h) {
    if (*h) {
        SetEvent(*h);
        CloseHandle(*h);
        *h = nullptr;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API (any thread)
// ---------------------------------------------------------------------------

bool Renderer::Start() {
    QueryPerformanceFrequency(&qpcFreq_);
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool ok = false;
    thread_ = std::thread([this, ready, &ok] { ThreadMain(ready, &ok); });
    WaitForSingleObject(ready, INFINITE);
    CloseHandle(ready);
    return ok;
}

void Renderer::Stop() {
    if (!thread_.joinable()) return;
    stop_ = true;
    SetEvent(wake_);
    thread_.join();
    CloseHandle(wake_);
    wake_ = nullptr;
}

ComPtr<ID3D11Device> Renderer::Device() {
    AcquireSRWLockShared(&deviceLock_);
    ComPtr<ID3D11Device> d = device_;
    ReleaseSRWLockShared(&deviceLock_);
    return d;
}

void Renderer::Retain(HWND hwnd) {
    AcquireSRWLockExclusive(&activeLock_);
    ++active_[hwnd];
    ReleaseSRWLockExclusive(&activeLock_);
}

bool Renderer::IsActive(HWND hwnd) {
    AcquireSRWLockShared(&activeLock_);
    bool active = active_.find(hwnd) != active_.end();
    ReleaseSRWLockShared(&activeLock_);
    return active;
}

void Renderer::Submit(GenieJob* job) {
    AcquireSRWLockExclusive(&queueLock_);
    queue_.push_back(job);
    ReleaseSRWLockExclusive(&queueLock_);
    SetEvent(wake_);
}

void Renderer::FailJob(GenieJob* job) {
    FinishWaiter(job);
}

// Tells the DLL its animation is over (it then reveals / tears down) and drops the job.
void Renderer::FinishWaiter(GenieJob* job) {
    SignalAndClose(&job->hFirst);
    SignalAndClose(&job->hDone);
    ReleaseJob(job);
}

void Renderer::ReleaseJob(GenieJob* job) {
    if (job->hFirst) CloseHandle(job->hFirst);
    if (job->hDone) CloseHandle(job->hDone);
    if (job->hRevealed) CloseHandle(job->hRevealed);
    AcquireSRWLockExclusive(&activeLock_);
    auto it = active_.find(job->hwnd);
    if (it != active_.end() && --it->second <= 0) active_.erase(it);
    ReleaseSRWLockExclusive(&activeLock_);
    delete job;
}

// ---------------------------------------------------------------------------
// Render thread
// ---------------------------------------------------------------------------

void Renderer::ThreadMain(HANDLE ready, bool* ok) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = GhostWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = MACWIN_GHOST_WNDCLASS;
    RegisterClassExW(&wc);

    *ok = CreateDevice();
    SetEvent(ready);
    if (!*ok) return;

    while (!stop_) {
        std::vector<GenieJob*> jobs;
        AcquireSRWLockExclusive(&queueLock_);
        jobs.swap(queue_);
        ReleaseSRWLockExclusive(&queueLock_);
        if (!jobs.empty() && DeviceLost()) RecoverDevice();
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        for (GenieJob* j : jobs) {
            if (broken_) FailJob(j);
            else StartJob(j, now);
        }

        PumpMessages();
        if (anims_.empty() && superseded_.empty()) {
            // Idle: sleep until a job arrives (no polling, no timer).
            MsgWaitForMultipleObjectsEx(1, &wake_, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        QueryPerformanceCounter(&now);
        for (auto& a : anims_) StepAnim(a.get(), now);
        if (DeviceLost()) {
            RecoverDevice();   // finishes every animation
            continue;
        }
        ProcessSuperseded(now, false);
        if (anims_.empty()) {
            // Only superseded jobs left, waiting for their window to settle.
            MsgWaitForMultipleObjectsEx(1, &wake_, 15, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }
        dcomp_->Commit();

        if (!WaitForTick()) continue;   // woken by a new job, not by the compositor

        // Everything presented before this tick is on screen now.
        for (size_t i = 0; i < anims_.size();) {
            Anim* a = anims_[i].get();
            if (a->firstPending) {
                a->firstPending = false;
                for (GenieJob* w : a->waiters) {
                    if (w->hFirst) SetEvent(w->hFirst);
                }
            }
            if (a->phase == Anim::kRetire) {
                RetireAnim(a);
                anims_.erase(anims_.begin() + i);
            } else {
                ++i;
            }
        }
        if (anims_.empty()) dcomp_->Commit();
    }

    AbortAll();
    std::vector<GenieJob*> pending;
    AcquireSRWLockExclusive(&queueLock_);
    pending.swap(queue_);
    ReleaseSRWLockExclusive(&queueLock_);
    for (GenieJob* j : pending) FailJob(j);
    for (auto& s : slots_) {
        if (s->hwnd) DestroyWindow(s->hwnd);
    }
    slots_.clear();
    cache_.Clear();
    DestroyDevice();
}

// Any GPU (AMD, Intel, NVIDIA, a basic display adapter) or none: a driver that fails
// anywhere on the way gets WARP, Direct3D on the CPU, which is plenty for one mesh.
bool Renderer::CreateDevice() {
    if (CreateDeviceOn(D3D_DRIVER_TYPE_HARDWARE)) return true;
    DestroyDevice();
    Log(L"render: hardware device unusable, falling back to WARP");
    return CreateDeviceOn(D3D_DRIVER_TYPE_WARP);
}

bool Renderer::CreateDeviceOn(D3D_DRIVER_TYPE type) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    D3D_FEATURE_LEVEL fl = {};
    warp_ = type == D3D_DRIVER_TYPE_WARP;
    HRESULT hr = D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, dev.put(), &fl, ctx.put());
    if (FAILED(hr)) {
        Log(L"render: D3D11CreateDevice(%ls) failed: 0x%08lX", warp_ ? L"WARP" : L"hardware", (unsigned long)hr);
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(dev.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(adapter.put())) ||
        FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void()))) {
        Log(L"render: DXGI setup failed");
        return false;
    }
    DXGI_ADAPTER_DESC ad = {};
    adapter->GetDesc(&ad);
    // Minimize snapshots wait on the GPU for the restore. Integrated graphics and WARP
    // take that memory from the system RAM, so they get a smaller budget.
    const size_t mb = 1ull << 20;
    cache_.SetBudget(warp_ ? 96 * mb : ad.DedicatedVideoMemory >= (2ull << 30) ? 384 * mb : 160 * mb);

    if (!dcompDll_) dcompDll_ = LoadLibraryExW(L"dcomp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    typedef HRESULT(WINAPI * CreateDevice_t)(IDXGIDevice*, REFIID, void**);
    auto createDComp = dcompDll_ ? (CreateDevice_t)GetProcAddress(dcompDll_, "DCompositionCreateDevice") : nullptr;
    waitClock_ = dcompDll_ ? (WaitClock_t)GetProcAddress(dcompDll_, "DCompositionWaitForCompositorClock") : nullptr;
    ComPtr<IDCompositionDevice> dcomp;
    if (!createDComp || FAILED(createDComp(dxgiDevice.get(), __uuidof(IDCompositionDevice), dcomp.put_void()))) {
        Log(L"render: DirectComposition unavailable");
        return false;
    }

    AcquireSRWLockExclusive(&deviceLock_);
    device_ = dev;
    ctx_ = ctx;
    factory_ = factory;
    dcomp_ = dcomp;
    ReleaseSRWLockExclusive(&deviceLock_);

    UINT quality = 0;
    msaaCount_ = 1;
    if (SUCCEEDED(device_->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, 4, &quality)) && quality > 0) {
        msaaCount_ = 4;
    }

    if (!CompileShaders()) return false;

    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbd.ByteWidth = sizeof(GenieConstants);
    device_->CreateBuffer(&cbd, nullptr, genieCB_.put());
    cbd.ByteWidth = sizeof(PrepConstants);
    device_->CreateBuffer(&cbd, nullptr, prepCB_.put());

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_ANISOTROPIC;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxAnisotropy = 8;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = FLT_MAX;
    device_->CreateSamplerState(&sd, sampler_.put());

    // Premultiplied "over" (the warped mesh can fold onto itself near the neck).
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device_->CreateBlendState(&bd, blend_.put());

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = msaaCount_ > 1;
    device_->CreateRasterizerState(&rd, raster_.put());

    if (!genieCB_ || !prepCB_ || !sampler_ || !blend_ || !raster_) {
        Log(L"render: pipeline state creation failed");
        return false;
    }
    Log(L"render: %ls%ls, feature level %x, MSAA %ux, compositor clock %ls", ad.Description,
        warp_ ? L" (WARP)" : L"", (unsigned)fl, msaaCount_, waitClock_ ? L"yes" : L"no (DwmFlush)");
    return true;
}

bool Renderer::DeviceLost() {
    return device_ && device_->GetDeviceRemovedReason() != S_OK;
}

void Renderer::RecoverDevice() {
    Log(L"render: graphics device lost (0x%08lX), making it again",
        (unsigned long)(device_ ? device_->GetDeviceRemovedReason() : E_FAIL));
    AbortAll();   // the DLLs reveal their windows
    for (auto& s : slots_) {
        if (s->hwnd) DestroyWindow(s->hwnd);
    }
    slots_.clear();
    cache_.Clear();
    DestroyDevice();
    // A driver that is being updated or reset can take a few seconds to come back.
    for (int attempt = 0; attempt < 8 && !stop_; ++attempt) {
        if (CreateDevice()) {
            broken_ = false;
            Log(L"render: device ready again");
            return;
        }
        DestroyDevice();
        WaitForSingleObject(wake_, 250u << (attempt < 4 ? attempt : 4));
    }
    broken_ = true;
    Log(L"render: no graphics device; windows keep the standard animations until MacWin restarts");
}

void Renderer::DestroyDevice() {
    grids_.clear();
    genieVS_.reset();
    classicVS_.reset();
    fullscreenVS_.reset();
    geniePS_.reset();
    prepPS_.reset();
    genieCB_.reset();
    prepCB_.reset();
    sampler_.reset();
    blend_.reset();
    raster_.reset();
    if (ctx_) ctx_->ClearState();
    AcquireSRWLockExclusive(&deviceLock_);
    dcomp_.reset();
    factory_.reset();
    ctx_.reset();
    device_.reset();
    ReleaseSRWLockExclusive(&deviceLock_);
}

bool Renderer::CompileShaders() {
    HMODULE compiler = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto compile = compiler ? (pD3DCompile)GetProcAddress(compiler, "D3DCompile") : nullptr;
    if (!compile) {
        Log(L"render: d3dcompiler_47.dll not available");
        return false;
    }
    auto build = [&](const char* entry, const char* target, ID3DBlob** out) -> bool {
        ComPtr<ID3DBlob> errors;
        HRESULT hr = compile(kGenieHlsl, sizeof(kGenieHlsl) - 1, "genie.hlsl", nullptr, nullptr, entry, target,
                             D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, errors.put());
        if (FAILED(hr)) {
            Log(L"render: shader %hs failed: %hs", entry,
                errors ? (const char*)errors->GetBufferPointer() : "(no message)");
            return false;
        }
        return true;
    };
    bool ok;
    {
        // The blobs are objects of d3dcompiler_47.dll: release them before unloading it.
        ComPtr<ID3DBlob> vs, cvs, ps, fvs, pps;
        ok = build("GenieVS", "vs_4_0", vs.put()) && build("ClassicVS", "vs_4_0", cvs.put()) &&
             build("GeniePS", "ps_4_0", ps.put()) && build("FullscreenVS", "vs_4_0", fvs.put()) &&
             build("PrepPS", "ps_4_0", pps.put());
        if (ok) {
            ok = SUCCEEDED(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr,
                                                       genieVS_.put())) &&
                 SUCCEEDED(device_->CreateVertexShader(cvs->GetBufferPointer(), cvs->GetBufferSize(), nullptr,
                                                       classicVS_.put())) &&
                 SUCCEEDED(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr,
                                                      geniePS_.put())) &&
                 SUCCEEDED(device_->CreateVertexShader(fvs->GetBufferPointer(), fvs->GetBufferSize(), nullptr,
                                                       fullscreenVS_.put())) &&
                 SUCCEEDED(device_->CreatePixelShader(pps->GetBufferPointer(), pps->GetBufferSize(), nullptr,
                                                      prepPS_.put()));
            if (!ok) Log(L"render: shader object creation failed");
        }
    }
    FreeLibrary(compiler);
    return ok;
}

// Triangle-list indices of a tiles x tiles grid, cached per tile count.
ID3D11Buffer* Renderer::GridIndices(int tiles, UINT* count) {
    *count = (UINT)(tiles * tiles * 6);
    auto it = grids_.find(tiles);
    if (it != grids_.end()) return it->second.get();
    std::vector<uint16_t> idx;
    idx.reserve(*count);
    const int stride = tiles + 1;
    for (int y = 0; y < tiles; ++y) {
        for (int x = 0; x < tiles; ++x) {
            uint16_t a = (uint16_t)(y * stride + x), b = (uint16_t)(a + 1);
            uint16_t c = (uint16_t)(a + stride), d = (uint16_t)(c + 1);
            idx.insert(idx.end(), {a, b, c, b, d, c});
        }
    }
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.ByteWidth = (UINT)(idx.size() * sizeof(uint16_t));
    D3D11_SUBRESOURCE_DATA init = {idx.data(), 0, 0};
    ComPtr<ID3D11Buffer> buf;
    if (FAILED(device_->CreateBuffer(&bd, &init, buf.put()))) return nullptr;
    ID3D11Buffer* raw = buf.get();
    grids_.emplace(tiles, std::move(buf));
    return raw;
}

// Crops the raw capture to the extended frame, fixes alpha and builds the mip chain:
// one tiny GPU pass per snapshot; the result is also what gets cached for restore.
bool Renderer::PrepareSnapshot(GenieJob* job, SnapshotCache::Entry* out) {
    ComPtr<ID3D11Device> rawDevice;
    job->raw->GetDevice(rawDevice.put());
    if (rawDevice.get() != device_.get()) return false;   // uploaded before a device reset

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)job->cropW;
    td.Height = (UINT)job->cropH;
    td.MipLevels = 0;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, tex.put()))) return false;
    tex->GetDesc(&td);

    D3D11_RENDER_TARGET_VIEW_DESC rtvd = {};
    rtvd.Format = td.Format;
    rtvd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv, rawSrv;
    if (FAILED(device_->CreateRenderTargetView(tex.get(), &rtvd, rtv.put())) ||
        FAILED(device_->CreateShaderResourceView(tex.get(), nullptr, srv.put())) ||
        FAILED(device_->CreateShaderResourceView(job->raw.get(), nullptr, rawSrv.put()))) {
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx_->Map(prepCB_.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    PrepConstants pc = {{job->cropX, job->cropY, job->forceOpaque ? 1 : 0, 0}};
    memcpy(m.pData, &pc, sizeof(pc));
    ctx_->Unmap(prepCB_.get(), 0);

    D3D11_VIEWPORT vp = {0, 0, (float)job->cropW, (float)job->cropH, 0, 1};
    ctx_->OMSetRenderTargets(1, rtv.addr(), nullptr);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    ctx_->RSSetState(raster_.get());
    ctx_->RSSetViewports(1, &vp);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(fullscreenVS_.get(), nullptr, 0);
    ctx_->PSSetShader(prepPS_.get(), nullptr, 0);
    ctx_->PSSetConstantBuffers(1, 1, prepCB_.addr());
    ctx_->PSSetShaderResources(0, 1, rawSrv.addr());
    ctx_->Draw(3, 0);
    ID3D11ShaderResourceView* nullSrv = nullptr;
    ctx_->PSSetShaderResources(0, 1, &nullSrv);
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    ctx_->GenerateMips(srv.get());

    out->tex = tex;
    out->srv = srv;
    out->w = job->cropW;
    out->h = job->cropH;
    size_t bytes = (size_t)job->cropW * job->cropH * 4;
    out->bytes = bytes + bytes / 3;   // + mip chain
    job->raw.reset();                  // the raw capture is no longer needed
    return true;
}

bool Renderer::SetupSlot(Slot* s) {
    s->hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                                  WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                              MACWIN_GHOST_WNDCLASS, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    if (!s->hwnd) return false;
    // Layered + transparent = click-through. Content comes from DirectComposition.
    SetLayeredWindowAttributes(s->hwnd, 0, 255, LWA_ALPHA);
    BOOL on = TRUE;
    DwmSetWindowAttribute(s->hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &on, sizeof(on));
    DwmSetWindowAttribute(s->hwnd, DWMWA_EXCLUDED_FROM_PEEK, &on, sizeof(on));

    DXGI_SWAP_CHAIN_DESC1 d = {};
    d.Width = 256;
    d.Height = 256;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.Scaling = DXGI_SCALING_STRETCH;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    HRESULT hr = factory_->CreateSwapChainForComposition(device_.get(), &d, nullptr, s->swap.put());
    if (SUCCEEDED(hr)) hr = dcomp_->CreateTargetForHwnd(s->hwnd, TRUE, s->target.put());
    if (SUCCEEDED(hr)) hr = dcomp_->CreateVisual(s->visual.put());
    if (SUCCEEDED(hr)) hr = s->visual->SetContent(s->swap.get());
    if (SUCCEEDED(hr)) hr = s->target->SetRoot(s->visual.get());
    if (FAILED(hr)) {
        Log(L"render: ghost setup failed: 0x%08lX", (unsigned long)hr);
        return false;
    }
    s->allocW = (int)d.Width;
    s->allocH = (int)d.Height;
    return CreateSlotTargets(s);
}

bool Renderer::CreateSlotTargets(Slot* s) {
    s->rtv.reset();
    s->msaa.reset();
    if (msaaCount_ > 1) {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = (UINT)s->allocW;
        td.Height = (UINT)s->allocH;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = msaaCount_;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, s->msaa.put()))) return false;
        return SUCCEEDED(device_->CreateRenderTargetView(s->msaa.get(), nullptr, s->rtv.put()));
    }
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(s->swap->GetBuffer(0, __uuidof(ID3D11Texture2D), back.put_void()))) return false;
    return SUCCEEDED(device_->CreateRenderTargetView(back.get(), nullptr, s->rtv.put()));
}

// Ghost windows are reused; buffers only ever grow (in 128 px steps), so a typical
// animation reuses everything and starts without any allocation.
Renderer::Slot* Renderer::AcquireSlot(int w, int h) {
    Slot* s = nullptr;
    for (auto& candidate : slots_) {
        if (!candidate->busy) {
            s = candidate.get();
            break;
        }
    }
    if (!s) {
        auto fresh = std::make_unique<Slot>();
        if (!SetupSlot(fresh.get())) {
            if (fresh->hwnd) DestroyWindow(fresh->hwnd);
            return nullptr;
        }
        slots_.push_back(std::move(fresh));
        s = slots_.back().get();
    }
    if (w > s->allocW || h > s->allocH) {
        int nw = s->allocW > RoundUp(w, 128) ? s->allocW : RoundUp(w, 128);
        int nh = s->allocH > RoundUp(h, 128) ? s->allocH : RoundUp(h, 128);
        s->rtv.reset();
        s->msaa.reset();
        ctx_->OMSetRenderTargets(0, nullptr, nullptr);
        ctx_->Flush();
        HRESULT hr = s->swap->ResizeBuffers(2, (UINT)nw, (UINT)nh, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
        if (FAILED(hr)) {
            Log(L"render: ResizeBuffers(%d x %d) failed: 0x%08lX", nw, nh, (unsigned long)hr);
            return nullptr;
        }
        s->allocW = nw;
        s->allocH = nh;
        if (!CreateSlotTargets(s)) return nullptr;
    }
    s->busy = true;
    return s;
}

Renderer::Anim* Renderer::FindAnim(HWND hwnd) {
    for (auto& a : anims_) {
        if (a->hwnd == hwnd) return a.get();
    }
    return nullptr;
}

void Renderer::StartJob(GenieJob* job, LARGE_INTEGER now) {
    if (Anim* running = FindAnim(job->hwnd)) {
        // A closing window's genie is never turned around (the window is going away), and
        // a close meeting a running genie is left to Windows.
        if (running->params.closing || job->params.closing) {
            FailJob(job);
            return;
        }
        Retarget(running, job, now);
        return;
    }

    auto a = std::make_unique<Anim>();
    // A fresh capture if the DLL sent one, else the minimize snapshot from the cache
    // (a request for a window whose genie just ended can arrive without one).
    bool ok = job->raw && PrepareSnapshot(job, &a->snap);
    if (!ok) ok = cache_.Take(job->hwnd, job->cropW, job->cropH, &a->snap);
    if (!ok) {
        Log(L"render: no snapshot for %p, skipping", (void*)job->hwnd);
        FailJob(job);
        return;
    }

    a->hwnd = job->hwnd;
    a->rising = job->rising;
    a->durationMs = job->durationMs;
    a->tiles = job->tiles;
    a->cornerRadiusPx = job->cornerRadiusPx;
    a->cornerSmoothing = job->cornerSmoothing;
    a->params = job->params;
    a->requestedAt = job->requestedAt;
    const GenieParams& g = a->params;
    a->slot = AcquireSlot(g.canvasWidth, g.canvasHeight);
    if (!a->slot) {
        FailJob(job);
        return;
    }
    Slot* s = a->slot;
    s->visual->SetClip(D2D_RECT_F{0, 0, (float)g.canvasWidth, (float)g.canvasHeight});
    if (HWND tray = FindTaskbarForMonitor(job->hMon)) a->insertAfter = tray;
    SetWindowPos(s->hwnd, a->insertAfter, g.canvasLeft, g.canvasTop, g.canvasWidth, g.canvasHeight,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    a->start = now;
    a->waiters.push_back(job);
    anims_.push_back(std::move(a));
}

// A request for a window whose genie is already in flight (fast clicking: restore while
// it is still minimizing, minimize again while it is still coming back, ...).
//  - same direction: the job just waits for the running genie,
//  - opposite direction: the genie turns around from exactly where it is. Both time
//    curves are symmetric (f(1 - x) = 1 - f(x)), so continuing at raw progress 1 - r
//    lands on the very same frame: no jump, no restart, any number of times.
void Renderer::Retarget(Anim* a, GenieJob* job, LARGE_INTEGER now) {
    job->raw.reset();   // the running genie keeps its snapshot
    double r = 1.0;
    if (a->phase == Anim::kRunning) {
        r = Ms(now.QuadPart - a->start.QuadPart) / (double)a->durationMs;
        if (r > 1.0) r = 1.0;
        if (r < 0.0) r = 0.0;
    }

    if (job->rising == a->rising) {
        a->waiters.push_back(job);
        if (a->phase != Anim::kRunning && job->hDone) SetEvent(job->hDone);   // already there
        if (a->shown) a->firstPending = true;
        return;
    }

    // The jobs of the old direction are released once their window has settled.
    for (GenieJob* w : a->waiters) superseded_.push_back({w, a->rising, now});
    a->waiters.clear();
    // Turned back to a direction it had before: those jobs simply wait again.
    for (size_t i = 0; i < superseded_.size();) {
        if (superseded_[i].job->hwnd == a->hwnd && superseded_[i].wasRising == job->rising) {
            a->waiters.push_back(superseded_[i].job);
            superseded_.erase(superseded_.begin() + i);
        } else {
            ++i;
        }
    }
    a->waiters.push_back(job);
    a->rising = job->rising;
    a->durationMs = job->durationMs;
    a->start.QuadPart = now.QuadPart - Ticks((1.0 - r) * a->durationMs);
    a->phase = Anim::kRunning;
    a->reversals++;
    if (a->shown) a->firstPending = true;
}

// Superseded minimize jobs are released right away (their DLL only re-enables DWM
// transitions; the restore already happened). A superseded restore is released only
// once the window is minimized again - its DLL then uncloaks a window nobody can see,
// instead of revealing it in the middle of the reversed genie.
void Renderer::ProcessSuperseded(LARGE_INTEGER now, bool force) {
    for (size_t i = 0; i < superseded_.size();) {
        Superseded& s = superseded_[i];
        bool release = force || !s.wasRising || !IsWindow(s.job->hwnd) || IsIconic(s.job->hwnd) ||
                       Ms(now.QuadPart - s.since.QuadPart) > 600.0;
        if (release) {
            FinishWaiter(s.job);
            superseded_.erase(superseded_.begin() + i);
        } else {
            ++i;
        }
    }
}

void Renderer::StepAnim(Anim* a, LARGE_INTEGER now) {
    if (a->phase == Anim::kRunning) {
        double elapsed = Ms(now.QuadPart - a->start.QuadPart);
        bool last = elapsed >= a->durationMs;
        float raw = (float)fmin(elapsed / (double)a->durationMs, 1.0);
        float eased = GenieEase(a->params, raw);
        // Minimize runs 0 -> 1, restore 1 -> 0: the same warp played in reverse.
        float t = a->rising ? 1.0f - eased : eased;

        // A collapsed mesh is never presented: the ghost stays hidden until the
        // first real frame (restore) or holds its last good frame (minimize).
        if (!GenieFrameDegenerate(a->params, t)) {
            if (!Draw(a, t)) {
                a->phase = Anim::kRetire;
                return;
            }
            if (!a->shown) {
                SetWindowPos(a->slot->hwnd, a->insertAfter, 0, 0, 0, 0,
                             SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
                a->shown = true;
                a->firstPending = true;
                a->firstFrameMs = Ms(now.QuadPart - a->requestedAt.QuadPart);
            }
        }
        if (last) {
            for (GenieJob* w : a->waiters) {
                if (w->hDone) SetEvent(w->hDone);
            }
            a->phase = Anim::kLinger;
            a->doneAt = now;
        }
    }
    if (a->phase == Anim::kLinger) {
        // Restore: keep the final frame up until the DLL has revealed the real window,
        // so there is never a gap between the two.
        bool revealed = !a->rising;
        for (GenieJob* w : a->waiters) {
            if (w->hRevealed && WaitForSingleObject(w->hRevealed, 0) == WAIT_OBJECT_0) revealed = true;
        }
        if (revealed || Ms(now.QuadPart - a->doneAt.QuadPart) > 150.0) a->phase = Anim::kRetire;
    }
}

bool Renderer::Draw(Anim* a, float t) {
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    Slot* s = a->slot;
    const GenieParams& g = a->params;

    UINT indexCount = 0;
    ID3D11Buffer* ib = GridIndices(a->tiles, &indexCount);
    if (!ib) return false;

    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx_->Map(genieCB_.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    GenieConstants c = {};
    c.win[0] = g.win.x;
    c.win[1] = g.win.y;
    c.win[2] = g.win.width;
    c.win[3] = g.win.height;
    c.icon[0] = g.icon.x;
    c.icon[1] = g.icon.y;
    c.icon[2] = g.icon.width;
    c.icon[3] = g.icon.height;
    c.canvas[0] = (float)g.canvasLeft;
    c.canvas[1] = (float)g.canvasTop;
    c.canvas[2] = (float)g.canvasWidth;
    c.canvas[3] = (float)g.canvasHeight;
    c.params[0] = t;
    // Radius in snapshot pixels (the snapshot may be a DPI-virtualized size).
    c.params[1] = g.win.width > 0 ? a->cornerRadiusPx * (float)a->snap.w / g.win.width : 0.0f;
    c.params[2] = (float)a->tiles;
    c.params[3] = GenieFade(g, t);
    c.tex[0] = (float)a->snap.w;
    c.tex[1] = (float)a->snap.h;
    mw::CornerShape(c.params[1], a->cornerSmoothing, c.tex[0], c.tex[1], &c.tex[2], &c.tex[3]);
    memcpy(m.pData, &c, sizeof(c));
    ctx_->Unmap(genieCB_.get(), 0);

    const float clear[4] = {0, 0, 0, 0};
    ctx_->OMSetRenderTargets(1, s->rtv.addr(), nullptr);
    ctx_->ClearRenderTargetView(s->rtv.get(), clear);
    D3D11_VIEWPORT vp = {0, 0, (float)g.canvasWidth, (float)g.canvasHeight, 0, 1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->RSSetState(raster_.get());
    ctx_->OMSetBlendState(blend_.get(), nullptr, 0xFFFFFFFF);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);
    ctx_->VSSetShader(g.style == kGenieClassic ? classicVS_.get() : genieVS_.get(), nullptr, 0);
    ctx_->VSSetConstantBuffers(0, 1, genieCB_.addr());
    ctx_->PSSetShader(geniePS_.get(), nullptr, 0);
    ctx_->PSSetConstantBuffers(0, 1, genieCB_.addr());
    ctx_->PSSetShaderResources(0, 1, a->snap.srv.addr());
    ctx_->PSSetSamplers(0, 1, sampler_.addr());
    ctx_->DrawIndexed(indexCount, 0, 0);

    if (s->msaa) {
        ComPtr<ID3D11Texture2D> back;
        if (FAILED(s->swap->GetBuffer(0, __uuidof(ID3D11Texture2D), back.put_void()))) return false;
        ctx_->ResolveSubresource(back.get(), 0, s->msaa.get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    }
    HRESULT hr = s->swap->Present(0, 0);
    if (FAILED(hr)) {
        Log(L"render: Present failed: 0x%08lX", (unsigned long)hr);
        return false;
    }
    QueryPerformanceCounter(&t1);
    a->frames++;
    a->cpuMs += Ms(t1.QuadPart - t0.QuadPart);
    return true;
}

void Renderer::RetireAnim(Anim* a) {
    if (a->slot) {
        ShowWindow(a->slot->hwnd, SW_HIDE);
        a->slot->busy = false;
        a->slot = nullptr;
    }
    Log(L"anim %p %ls (%ls): %d frames, %d reversal(s), %.3f ms CPU/frame, first frame %.1f ms after request",
        (void*)a->hwnd, a->params.closing ? L"close" : (a->rising ? L"restore/open" : L"minimize"),
        a->params.style == kGenieClassic ? L"classic" : L"modern", a->frames, a->reversals,
        a->frames ? a->cpuMs / a->frames : 0.0, a->firstFrameMs);
    // Ended minimized: keep the snapshot for the matching restore.
    if (!a->rising && !a->params.closing && a->snap.srv && IsWindow(a->hwnd)) {
        cache_.Put(a->hwnd, std::move(a->snap));
    }
    for (GenieJob* w : a->waiters) FinishWaiter(w);
    a->waiters.clear();
}

void Renderer::AbortAll() {
    for (auto& a : anims_) RetireAnim(a.get());
    anims_.clear();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    ProcessSuperseded(now, true);
}

void Renderer::PumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// Paces frames on the compositor clock (Windows 11): exactly one frame per composed
// frame, no busy loop and no 2x-refresh overdraw. Returns false when woken by a new
// job instead of a tick.
bool Renderer::WaitForTick() {
    if (waitClock_) {
        // The loop drains the job queue at the top of every iteration, so the
        // (auto-reset) wake signal needs no re-arming.
        return waitClock_(1, &wake_, 100) != WAIT_OBJECT_0;
    }
    DwmFlush();
    return true;
}
