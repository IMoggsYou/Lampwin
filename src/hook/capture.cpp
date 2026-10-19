#include <windows.h>
#include "hook/runtime.h"

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 2
#endif

// Window-only capture (the mod's CaptureNow): PrintWindow renders just this window, so
// the taskbar or windows behind it can never bleed into the genie, and it also works
// on a cloaked window. The DIB is backed by a pagefile section, so PrintWindow writes
// straight into memory the host maps - no extra copy. Alpha fix-ups happen on the GPU.
HANDLE CaptureToSection(HWND hWnd, int rawW, int rawH) {
    if (rawW <= 0 || rawH <= 0 || rawW > 16384 || rawH > 16384) return nullptr;
    const unsigned long long bytes = (unsigned long long)rawW * (unsigned long long)rawH * 4ull;
    HANDLE hSection = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                         (DWORD)(bytes >> 32), (DWORD)bytes, nullptr);
    if (!hSection) return nullptr;

    HDC hScreen = GetDC(nullptr);
    HDC hMem = CreateCompatibleDC(hScreen);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = rawW;
    bmi.bmiHeader.biHeight = -rawH;   // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hDib = CreateDIBSection(hScreen, &bmi, DIB_RGB_COLORS, &bits, hSection, 0);
    bool ok = false;
    if (hDib && bits) {
        HGDIOBJ old = SelectObject(hMem, hDib);
        ok = PrintWindow(hWnd, hMem, PW_RENDERFULLCONTENT) != FALSE;
        GdiFlush();
        SelectObject(hMem, old);
    }
    if (hDib) DeleteObject(hDib);
    DeleteDC(hMem);
    ReleaseDC(nullptr, hScreen);

    if (!ok) {
        CloseHandle(hSection);
        return nullptr;
    }
    return hSection;
}
