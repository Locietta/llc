module span;

public struct Span<T> {
    T *ptr;
    uint64_t count;

    public __subscript(uint64_t i)->T {
        get { return ptr[i]; }
    }

    public property uint64_t length {
        get { return count; }
    }
};

public struct MutSpan<T> {
    T *ptr;
    uint64_t count;

    public __subscript(uint64_t i)->T {
        get { return ptr[i]; }
        set { ptr[i] = newValue; }
    }

    public property uint64_t length {
        get { return count; }
    }

    public Span<T> asSpan() {
        Span<T> s;
        s.ptr = ptr;
        s.count = count;
        return s;
    }
};
