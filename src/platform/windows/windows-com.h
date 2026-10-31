#pragma once

#include <windows.h>
#include <objbase.h>
#include <unknwn.h>

#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) noexcept {}

    explicit ComPtr(T* pointer) noexcept : ptr(pointer) {}

    ComPtr(const ComPtr& other) noexcept : ptr(other.ptr) {
        if (ptr) {
            ptr->AddRef();
        }
    }

    ComPtr(ComPtr&& other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }

    ~ComPtr() { Reset(); }

    ComPtr& operator=(const ComPtr& other) noexcept {
        if (this != &other) {
            Reset();
            ptr = other.ptr;
            if (ptr) {
                ptr->AddRef();
            }
        }
        return *this;
    }

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }

    ComPtr& operator=(T* other) noexcept {
        if (ptr != other) {
            Reset();
            ptr = other;
            if (ptr) {
                ptr->AddRef();
            }
        }
        return *this;
    }

    void Reset() noexcept {
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
    }

    void Attach(T* other) noexcept {
        Reset();
        ptr = other;
    }

    [[nodiscard]] T* Detach() noexcept {
        T* detached = ptr;
        ptr = nullptr;
        return detached;
    }

    [[nodiscard]] T* Get() const noexcept { return ptr; }

    [[nodiscard]] T** Put() noexcept {
        Reset();
        return &ptr;
    }

    [[nodiscard]] T** GetAddressOf() noexcept { return &ptr; }

    T* operator->() const noexcept { return ptr; }

    T& operator*() const noexcept { return *ptr; }

    explicit operator bool() const noexcept { return ptr != nullptr; }

    template <typename Other>
    HRESULT As(REFIID iid, ComPtr<Other>& output) const noexcept {
        output.Reset();
        if (!ptr) {
            return E_POINTER;
        }
        return ptr->QueryInterface(iid, reinterpret_cast<void**>(output.Put()));
    }

    [[nodiscard]] bool Supports(REFIID iid) const noexcept {
        if (!ptr) {
            return false;
        }
        void* raw = nullptr;
        const HRESULT result = ptr->QueryInterface(iid, &raw);
        if (SUCCEEDED(result) && raw) {
            static_cast<IUnknown*>(raw)->Release();
            return true;
        }
        return false;
    }

private:
    T* ptr = nullptr;
};

template <typename Interface>
class ComCallback : public Interface {
public:
    ComCallback() = default;
    virtual ~ComCallback() = default;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (!object) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == InterfaceId()) {
            *object = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(referenceCount.fetch_add(1) + 1); }

    ULONG STDMETHODCALLTYPE Release() override {
        const LONG remaining = referenceCount.fetch_sub(1) - 1;
        if (remaining == 0) {
            delete this;
        }
        return static_cast<ULONG>(remaining);
    }

protected:
    virtual const IID& InterfaceId() const = 0;

private:
    std::atomic<LONG> referenceCount{1};
};

std::wstring Utf8ToWide(std::string_view text);
std::string WideToUtf8(std::wstring_view text);
std::string FormatHResult(HRESULT result);
std::string PointerToHex(const void* pointer);
