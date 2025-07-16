#pragma once

#include "types.h"
#include <cassert>
#include <concepts>

namespace loia {

// Reference counting object, without atomic counter
struct RefObject {
    RefObject() : ref_count_(0) {}
    RefObject(const RefObject &) : ref_count_(0) {}
    RefObject &operator=(const RefObject &) { return *this; }

    virtual ~RefObject() {}

    u64 increase_ref() noexcept { return ++ref_count_; }
    u64 decrease_ref() noexcept { return --ref_count_; }

    u64 release() {
        assert(ref_count_ != 0);
        if (decrease_ref() == 0) {
            delete this;
            return 0;
        }
        return ref_count_;
    }

    bool is_unique() const {
        assert(ref_count_ != 0);
        return ref_count_ == 1;
    }

    u64 debug_get_ref_count() const noexcept { return ref_count_; }

private:
    u64 ref_count_;
};

template <std::derived_from<RefObject> T>
struct RefPtr {
    RefPtr() : ptr_(nullptr) {}
    RefPtr(T *ptr) : ptr_(ptr) {
        if (ptr_) {
            ptr_->increase_ref();
        }
    }
    RefPtr(RefPtr const &other) : ptr_(other.ptr_) {
        if (ptr_) {
            ptr_->increase_ref();
        }
    }
    RefPtr(RefPtr &&other) : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    template <typename U>
        requires std::convertible_to<U *, T *>
    RefPtr(RefPtr<U> const &other) : ptr_(other.get()) {
        if (ptr_) {
            ptr_->increase_ref();
        }
    }

    void operator=(RefPtr<T> const &other) {
        T *old = ptr_;
        if (other.ptr_) {
            other.ptr_->increase_ref();
        }
        ptr_ = other.ptr_;
        if (old) {
            old->release();
        }
    }

    void operator=(RefPtr<T> &&other) {
        // rely on moved-from object to release
        swap(other);
    }

    ~RefPtr() {
        if (ptr_) {
            ptr_->release();
        }
    }

    bool operator==(const T *ptr) const { return ptr_ == ptr; }
    bool operator==(const RefPtr<T> &other) const { return ptr_ == other.ptr_; }

    T *get() const { return ptr_; }
    T *operator->() const { return ptr_; }
    T &operator*() const { return *ptr_; }
    operator bool() const { return ptr_ != nullptr; }

    void attach(T *ptr) {
        T *old = ptr_;
        ptr_ = ptr;
        if (old) {
            old->release();
        }
    }

    T *detach() {
        T *old = ptr_;
        ptr_ = nullptr;
        return old;
    }

    void swap(RefPtr<T> &other) {
        T *old = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = old;
    }

    void reset() {
        if (ptr_) {
            ptr_->release();
            ptr_ = nullptr;
        }
    }

private:
    T *ptr_;
};

} // namespace loia