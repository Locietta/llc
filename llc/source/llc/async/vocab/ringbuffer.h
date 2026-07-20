#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>

namespace llc {

class RingBuffer {
public:
    /// contract: pre(cap > 0)
    explicit RingBuffer(size_t cap = 64 * 1024)
        : storage(std::make_unique_for_overwrite<char[]>(cap)), capacity(cap) {
        assert(cap > 0 && "RingBuffer capacity must be greater than zero");
    }

    size_t readable_bytes() const {
        return size;
    }

    size_t writable_bytes() const {
        return capacity - size;
    }

    size_t read(char *dest, size_t len);

    std::pair<const char *, size_t> get_read_ptr() const;
    void advance_read(size_t len);

    std::pair<char *, size_t> get_write_ptr();
    void advance_write(size_t len);

private:
    std::unique_ptr<char[]> storage;
    size_t capacity;
    size_t head = 0;
    size_t tail = 0;
    size_t size = 0;
};

} // namespace llc
