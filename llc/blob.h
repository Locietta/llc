#include <slang.h>
#include <slang-com-ptr.h>

#include <span>
#include <cstring>
#include <atomic>
#include <memory>
#include <filesystem>
#include <fmt/base.h>

#include <llc/types.hpp>

namespace llc {

// provide a blob implementation that can be inited from a file
struct FileBlob final : slang::IBlob {
    FileBlob(const FileBlob &) = delete;
    FileBlob &operator=(const FileBlob &) = delete;

    // copy
    FileBlob(std::span<const byte> data) : ref_count_{1} {
        data_ = new byte[data.size()];
        std::memcpy(data_, data.data(), data.size());
        size_ = data.size();
    }

    // take ownership of data
    FileBlob(std::unique_ptr<byte[]> &&data, usize size) noexcept : ref_count_{1} {
        data_ = data.release();
        size_ = size;
    }

    ~FileBlob() {
        delete[] data_;
    }

    // ISlangUnknown
    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(
        SlangUUID const &guid,
        void **out_object) override;

    // IUnknown
    SLANG_NO_THROW u32 SLANG_MCALL addRef() override { return ++ref_count_; }
    SLANG_NO_THROW u32 SLANG_MCALL release() override {
        auto new_count = --ref_count_;
        if (new_count == 0) {
            delete this;
        }
        return new_count;
    }

    // IBlob
    SLANG_NO_THROW const void *SLANG_MCALL getBufferPointer() override {
        return data_;
    }
    SLANG_NO_THROW size_t SLANG_MCALL getBufferSize() override {
        return size_;
    }

    std::atomic<u32> ref_count_{0};
    byte *data_{nullptr};
    usize size_{0};

    // load from file
    static Slang::ComPtr<FileBlob> load(const char *path);
    static Slang::ComPtr<FileBlob> load(std::filesystem::path const &path);
};

inline void diagnose_if_needed(slang::IBlob *diagnostics) {
    if (diagnostics != nullptr) {
        fmt::print("{}", (const char *) diagnostics->getBufferPointer());
    }
}

} // namespace llc