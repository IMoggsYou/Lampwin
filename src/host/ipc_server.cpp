// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/IMoggsYou/MacWin

#include "host/ipc_server.h"
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <math.h>
#include "host/log.h"
#include "host/render/device.h"
#include "host/settings_store.h"
#include "host/taskbar_locator.h"

namespace {

HANDLE DupFrom(HANDLE hProc, uint64_t value) {
    if (!value) return nullptr;
    HANDLE out = nullptr;
    if (!DuplicateHandle(hProc, (HANDLE)(uintptr_t)value, GetCurrentProcess(), &out, 0, FALSE,
                         DUPLICATE_SAME_ACCESS)) {
        return nullptr;
    }
    return out;
}

void CloseJobHandles(GenieJob* job) {
    if (job->hFirst) CloseHandle(job->hFirst);
    if (job->hDone) CloseHandle(job->hDone);
    if (job->hRevealed) CloseHandle(job->hRevealed);
    job->hFirst = job->hDone = job->hRevealed = nullptr;
}

}  // namespace

LRESULT IpcServer::Handle(const COPYDATASTRUCT* cds) {
    if (!cds || cds->dwData != MW_IPC_ANIM || cds->cbData != sizeof(AnimRequest) || !cds->lpData) {
        return MW_REPLY_FAIL;
    }
    AnimRequest req;
    memcpy(&req, cds->lpData, sizeof(req));
    return HandleAnim(req);
}

uint32_t IpcServer::HandleAnim(const AnimRequest& r) {
    LARGE_INTEGER requestedAt;
    QueryPerformanceCounter(&requestedAt);
    const HostSettings& s = settings_.Get();
    HWND hwnd = (HWND)(uintptr_t)r.hwnd;
    static const wchar_t* const kSources[] = {L"?", L"ShowWindow", L"DefWindowProc", L"SetWindowPlacement",
                                              L"CloseWindow", L"HCBT_MINMAX", L"launch", L"close"};
    static const wchar_t* const kKinds[] = {L"minimize", L"restore", L"open", L"close"};
    uint32_t source = (r.flags & MW_REQ_SOURCE_MASK) >> MW_REQ_SOURCE_SHIFT;
    wchar_t snap[40] = L"";
    if (r.flags & MW_REQ_HAS_SNAPSHOT) swprintf(snap, 40, L", snapshot in %u ms", r.flags >> MW_REQ_CAPTURE_MS_SHIFT);
    Log(L"ipc: %ls request for %p (pid %u, %d x %d%ls, via %ls%ls)", r.kind < 4 ? kKinds[r.kind] : L"?",
        (void*)hwnd, r.pid, r.rawW, r.rawH, snap, source < 8 ? kSources[source] : L"?",
        (r.flags & MW_REQ_SENT) ? L", sent by another thread" : L"");
    if (r.version != MW_IPC_VERSION || !s.enabled || !IsWindow(hwnd) || r.kind > MW_ANIM_CLOSE) {
        Log(L"ipc: rejected (version %u, enabled %d, window %d)", r.version, (int)s.enabled, (int)IsWindow(hwnd));
        return MW_REPLY_FAIL;
    }
    // Opening / closing: only with the genie chosen for it (the DLL only asks then).
    const bool opening = r.kind == MW_ANIM_LAUNCH, closing = r.kind == MW_ANIM_CLOSE;
    if ((opening && s.genie.openEffect != MW_EFFECT_GENIE) || (closing && s.genie.closeEffect != MW_EFFECT_GENIE)) {
        return MW_REPLY_FAIL;
    }
    if (r.rawW <= 0 || r.rawH <= 0 || r.rawW > 16384 || r.rawH > 16384 ||
        !(r.cropU0 >= 0 && r.cropV0 >= 0 && r.cropU1 <= 1.0f && r.cropV1 <= 1.0f && r.cropU1 > r.cropU0 &&
          r.cropV1 > r.cropV0)) {
        Log(L"ipc: rejected (bad size / crop %.3f %.3f %.3f %.3f)", r.cropU0, r.cropV0, r.cropU1, r.cropV1);
        return MW_REPLY_FAIL;
    }

    const bool rising = r.kind == MW_ANIM_RESTORE || r.kind == MW_ANIM_LAUNCH;
    const bool hasSnapshot = (r.flags & MW_REQ_HAS_SNAPSHOT) && r.hSection;
    // Crop of the extended frame in snapshot pixels.
    int cropX = (int)lroundf(r.cropU0 * r.rawW);
    int cropY = (int)lroundf(r.cropV0 * r.rawH);
    int cropW = (int)lroundf(r.cropU1 * r.rawW) - cropX;
    int cropH = (int)lroundf(r.cropV1 * r.rawH) - cropY;
    if (cropW < 1 || cropH < 1) return MW_REPLY_FAIL;

    // A window whose genie is still in flight needs no snapshot: the running genie keeps
    // its own and simply turns around (see Renderer::Retarget).
    // An opening window is always captured fresh: a cached minimize snapshot of the same
    // handle would be an old picture.
    const bool active = renderer_.IsActive(hwnd);
    if (rising && !hasSnapshot && !active && (opening || !renderer_.HasSnapshot(hwnd, cropW, cropH))) {
        return MW_REPLY_NEED_SNAPSHOT;
    }

    GenieJob* job = new GenieJob();
    job->hwnd = hwnd;
    job->rising = rising;
    job->cropX = cropX;
    job->cropY = cropY;
    job->cropW = cropW;
    job->cropH = cropH;
    job->requestedAt = requestedAt;
    job->durationMs = opening ? s.genie.openDurationMs : (closing ? s.genie.closeDurationMs : s.genie.durationMs);
    job->tiles = s.genie.meshQuality;
    job->style = s.genie.style;
    job->params.closing = closing;
    renderer_.Retain(hwnd);   // from here on every exit goes through FailJob / the renderer

    HANDLE hProc = OpenProcess(PROCESS_DUP_HANDLE, FALSE, r.pid);
    HANDLE hSection = nullptr;
    if (hProc) {
        job->hDone = DupFrom(hProc, r.hDone);
        job->hFirst = DupFrom(hProc, r.hFirstFrame);
        job->hRevealed = DupFrom(hProc, r.hRevealed);
        if (hasSnapshot) hSection = DupFrom(hProc, r.hSection);
        CloseHandle(hProc);
    }
    bool ok = job->hDone && (!rising || job->hRevealed) && (rising || job->hFirst);
    // Uploaded even for an active window: its genie may end before this job is picked up.
    if (ok && hasSnapshot) ok = hSection && UploadSnapshot(hSection, job, r.rawW, r.rawH);
    if (hSection) CloseHandle(hSection);
    if (!ok) {
        Log(L"ipc: request for %p (pid %u) rejected: handles/snapshot unavailable", (void*)hwnd, r.pid);
        CloseJobHandles(job);
        renderer_.FailJob(job);   // nothing left to signal; just releases the window
        return MW_REPLY_FAIL;
    }

    // Geometry in physical pixels (this process is per-monitor DPI aware).
    RECT rect;
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))) &&
        !GetWindowRect(hwnd, &rect)) {
        renderer_.FailJob(job);
        return MW_REPLY_FAIL;
    }
    job->winRect = rect;
    // Multi-monitor support targets the window's monitor; otherwise the primary.
    job->hMon = s.genie.multiMonitor ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
                                     : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (!IsZoomed(hwnd)) {
        UINT dpiX = 96, dpiY = 96;
        GetDpiForMonitor(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        job->cornerRadiusPx = (float)s.genie.cornerRadius * (float)dpiY / 96.0f;
        job->cornerSmoothing = (float)s.genie.cornerSmoothing / 100.0f;
        // Square corners asked of DWM. Not for a window MacWin cut to its corners (it has
        // a region): MacWin itself sets that preference there (see corners.cpp).
        DWORD pref = 0;
        RECT rgnBox;
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref))) &&
            pref == DWMWCP_DONOTROUND && GetWindowRgnBox(hwnd, &rgnBox) == ERROR) {
            job->cornerRadiusPx = 0;
        }
    }

    // Dock target. Cursor over the taskbar = the user clicked THIS window's button, the
    // only fully reliable per-window signal when windows share a process (the mod's
    // rule). Otherwise the cached taskbar-button match, or a background UIA scan.
    POINT pt;
    GetCursorPos(&pt);
    MONITORINFO cmi = {sizeof(cmi)};
    RECT work;
    if (GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &cmi)) {
        work = cmi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }
    // Pointer outside the work area = on the taskbar: most likely a press on this
    // window's own button.
    const bool onTaskbar = !PtInRect(&work, pt);
    DockTarget dock;
    if (!locator_.TryResolve(hwnd, job->hMon, onTaskbar ? &pt : nullptr, &dock)) {
        DockTarget fallback = locator_.Fallback(hwnd, job->hMon, DefaultDockX(job->hMon));
        if (active) {
            // Joins / reverses a running genie, which keeps its own dock target.
            dock = fallback;
        } else {
            locator_.ResolveAsync(hwnd, job->hMon, fallback, [this, job](DockTarget t) { Dispatch(job, t); });
            return MW_REPLY_OK;
        }
    }
    Dispatch(job, dock);
    return MW_REPLY_OK;
}

// Called on the IPC thread, or on the locator thread after a UIA scan.
void IpcServer::Dispatch(GenieJob* job, const DockTarget& dock) {
    if (dock.hasRect) {
        Log(L"ipc: %p docks into its taskbar icon at %d,%d (icon %d px)", (void*)job->hwnd, dock.x, dock.y, dock.size);
    } else {
        Log(L"ipc: %p docks at x %d on the taskbar edge (button unknown)", (void*)job->hwnd, dock.x);
    }
    if (!ComputeGenieParams(job->winRect, job->hMon, dock, job->style, &job->params)) {
        renderer_.FailJob(job);
        return;
    }
    renderer_.Submit(job);
}

// Maps the DLL's capture, decides the alpha fallback and uploads it. PrintWindow wrote
// straight into this section, so this is the only copy on its way to the GPU.
bool IpcServer::UploadSnapshot(HANDLE hSection, GenieJob* job, int rawW, int rawH) {
    const size_t bytes = (size_t)rawW * rawH * 4;
    const DWORD* px = (const DWORD*)MapViewOfFile(hSection, FILE_MAP_READ, 0, 0, bytes);
    if (!px) return false;

    // The mod's fallback: if most of the extended frame came back fully transparent,
    // the alpha channel isn't describing real translucency - treat it as opaque.
    // Sampling every 4th pixel each way is plenty for a majority vote.
    size_t samples = 0, zero = 0;
    for (int y = job->cropY; y < job->cropY + job->cropH && y < rawH; y += 4) {
        const DWORD* row = px + (size_t)y * rawW;
        for (int x = job->cropX; x < job->cropX + job->cropW && x < rawW; x += 4) {
            ++samples;
            if ((row[x] >> 24) == 0) ++zero;
        }
    }
    job->forceOpaque = samples > 0 && zero * 2 > samples;

    ComPtr<ID3D11Device> device = renderer_.Device();
    bool ok = false;
    if (device) {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = (UINT)rawW;
        td.Height = (UINT)rawH;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init = {px, (UINT)(rawW * 4), 0};
        HRESULT hr = device->CreateTexture2D(&td, &init, job->raw.put());
        ok = SUCCEEDED(hr);
        if (!ok) Log(L"ipc: snapshot upload %d x %d failed: 0x%08lX", rawW, rawH, (unsigned long)hr);
    }
    UnmapViewOfFile(px);
    return ok;
}
