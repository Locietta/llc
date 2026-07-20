#include "ringbuffer.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace llc {

size_t RingBuffer::read(char *dest, size_t len) {
    const size_t to_read = std::min(len, size);
    if (to_read == 0) {
        return 0;
    }

    const size_t first_chunk = std::min(to_read, capacity - head);
    std::memcpy(dest, storage.get() + head, first_chunk);

    const size_t remaining = to_read - first_chunk;
    if (remaining > 0) {
        std::memcpy(dest + first_chunk, storage.get(), remaining);
    }

    head = (head + to_read) % capacity;
    size -= to_read;
    return to_read;
}

std::pair<const char *, size_t> RingBuffer::get_read_ptr() const {
    if (size == 0) {
        return {nullptr, 0};
    }

    // When the buffer is full, head == tail but size > 0. Using `>=` here
    // would yield contiguous = 0, causing read_chunk() to return an empty
    // span and the caller to spin forever. Use strict `>` so the full case
    // falls through to the else branch (capacity - head), which is correct.
    size_t contiguous = 0;
    if (tail > head) {
        contiguous = tail - head;
    } else {
        contiguous = capacity - head;
    }

    assert(contiguous > 0 && "get_read_ptr: non-empty buffer must yield contiguous > 0");
    return {storage.get() + head, contiguous};
}

void RingBuffer::advance_read(size_t len) {
    if (len > size) {
        len = size;
    }

    head = (head + len) % capacity;
    size -= len;
}

std::pair<char *, size_t> RingBuffer::get_write_ptr() {
    const size_t writable = writable_bytes();
    size_t contiguous = 0;
    if (writable == 0) {
        contiguous = 0;
    } else if (tail >= head) {
        contiguous = std::min(writable, capacity - tail);
    } else {
        contiguous = head - tail;
    }

    return {storage.get() + tail, contiguous};
}

void RingBuffer::advance_write(size_t len) {
    const size_t writable = writable_bytes();
    if (len > writable) {
        len = writable;
    }

    tail = (tail + len) % capacity;
    size += len;
}

} // namespace llc
