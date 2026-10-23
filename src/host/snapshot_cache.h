// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

#pragma once
#include <windows.h>
#include <d3d11.h>
#include <stdint.h>
#include <unordered_map>
#include "host/com.h"

// Minimize snapshots kept on the GPU for the restore animation, keyed by window.
// Bounded by a byte budget (least recently stored is evicted first) and emptied as
// windows are destroyed. Thread-safe: the IPC thread asks Has(), the render thread
// Put()s and Take()s, the event thread Erase()s.
class SnapshotCache {
public:
    struct Entry {
        ComPtr<ID3D11Texture2D> tex;
        ComPtr<ID3D11ShaderResourceView> srv;
        int w = 0, h = 0;
        size_t bytes = 0;
        uint64_t stamp = 0;
    };

    void SetBudget(size_t bytes) { budget_ = bytes; }
    void Put(HWND hwnd, Entry e);
    bool Take(HWND hwnd, int w, int h, Entry* out);   // removes the entry
    bool Has(HWND hwnd, int w, int h);
    void Erase(HWND hwnd);
    void Clear();

private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    std::unordered_map<HWND, Entry> map_;
    size_t total_ = 0;
    size_t budget_ = 512ull << 20;
    uint64_t clock_ = 0;
};
