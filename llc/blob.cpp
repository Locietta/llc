#include "blob.h"

#include <filesystem>
#include <fstream>

namespace llc {

SLANG_NO_THROW SlangResult SLANG_MCALL FileBlob::queryInterface(
    SlangUUID const &guid,
    void **out_object) {

    if (!out_object)
        return SLANG_E_INVALID_ARG;

    if (guid == ISlangBlob::getTypeGuid() || guid == ISlangUnknown::getTypeGuid()) {
        addRef();
        *out_object = static_cast<ISlangBlob *>(this);
        return SLANG_OK;
    }
    *out_object = nullptr;
    return SLANG_E_NO_INTERFACE;
}

Slang::ComPtr<FileBlob> FileBlob::load(std::filesystem::path const &path) {
    if (!std::filesystem::exists(path)) {
        return nullptr;
    }

    auto file_size = std::filesystem::file_size(path);
    usize size = static_cast<usize>(file_size);
    auto data = std::make_unique<byte[]>(size);

    std::ifstream file_stream(path, std::ios::binary);
    file_stream.read(reinterpret_cast<char *>(data.get()), file_size);

    return Slang::ComPtr<FileBlob>(new FileBlob(std::move(data), size));
}

Slang::ComPtr<FileBlob> FileBlob::load(const char *path) {
    const std::filesystem::path fs_path(path);
    return FileBlob::load(fs_path);
}

} // namespace llc