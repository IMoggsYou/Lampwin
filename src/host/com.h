// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/IMoggsYou/MacWin

#pragma once
#include <unknwn.h>
#include <utility>

// Minimal COM smart pointer (MinGW's WRL is incomplete).
template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) {}
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr& o) : p_(o.p_) {
        if (p_) p_->AddRef();
    }
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ComPtr& operator=(ComPtr o) noexcept {
        std::swap(p_, o.p_);
        return *this;
    }

    // Takes over an existing reference.
    static ComPtr Attach(T* p) {
        ComPtr c;
        c.p_ = p;
        return c;
    }

    void reset() {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }
    T* get() const { return p_; }
    T** put() {
        reset();
        return &p_;
    }
    void** put_void() { return reinterpret_cast<void**>(put()); }
    T* const* addr() const { return &p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

    template <class U>
    HRESULT As(ComPtr<U>* out) const {
        return p_ ? p_->QueryInterface(__uuidof(U), out->put_void()) : E_POINTER;
    }

private:
    T* p_ = nullptr;
};
