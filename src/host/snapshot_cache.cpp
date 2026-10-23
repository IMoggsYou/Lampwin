// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

#include "host/snapshot_cache.h"
#include <vector>

void SnapshotCache::Put(HWND hwnd, Entry e) {
    std::vector<Entry> dropped;   // released outside the lock
    AcquireSRWLockExclusive(&lock_);
    auto it = map_.find(hwnd);
    if (it != map_.end()) {
        total_ -= it->second.bytes;
        dropped.push_back(std::move(it->second));
        map_.erase(it);
    }
    e.stamp = ++clock_;
    total_ += e.bytes;
    map_.emplace(hwnd, std::move(e));
    while (total_ > budget_ && map_.size() > 1) {
        auto oldest = map_.begin();
        for (auto i = map_.begin(); i != map_.end(); ++i) {
            if (i->second.stamp < oldest->second.stamp) oldest = i;
        }
        total_ -= oldest->second.bytes;
        dropped.push_back(std::move(oldest->second));
        map_.erase(oldest);
    }
    ReleaseSRWLockExclusive(&lock_);
}

bool SnapshotCache::Take(HWND hwnd, int w, int h, Entry* out) {
    AcquireSRWLockExclusive(&lock_);
    auto it = map_.find(hwnd);
    bool ok = false;
    if (it != map_.end()) {
        ok = it->second.w == w && it->second.h == h;
        total_ -= it->second.bytes;
        if (ok) *out = std::move(it->second);
        map_.erase(it);   // a size mismatch makes the entry useless anyway
    }
    ReleaseSRWLockExclusive(&lock_);
    return ok;
}

bool SnapshotCache::Has(HWND hwnd, int w, int h) {
    AcquireSRWLockShared(&lock_);
    auto it = map_.find(hwnd);
    bool ok = it != map_.end() && it->second.w == w && it->second.h == h;
    ReleaseSRWLockShared(&lock_);
    return ok;
}

void SnapshotCache::Erase(HWND hwnd) {
    Entry dropped;
    AcquireSRWLockExclusive(&lock_);
    auto it = map_.find(hwnd);
    if (it != map_.end()) {
        total_ -= it->second.bytes;
        dropped = std::move(it->second);
        map_.erase(it);
    }
    ReleaseSRWLockExclusive(&lock_);
}

void SnapshotCache::Clear() {
    std::unordered_map<HWND, Entry> dropped;
    AcquireSRWLockExclusive(&lock_);
    dropped.swap(map_);
    total_ = 0;
    ReleaseSRWLockExclusive(&lock_);
}
