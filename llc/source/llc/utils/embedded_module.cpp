#include "embedded_module.h"

#include <span>
#include <llc/blob.h>

namespace llc {

Slang::ComPtr<slang::IModule> load_embedded_module(
    rhi::IDevice *device,
    EmbededModuleDesc const &desc) {

    auto blob = Slang::ComPtr<FileBlob>(new FileBlob(std::span<const byte>(
        reinterpret_cast<const byte *>(desc.start),
        reinterpret_cast<const byte *>(desc.end))));

    Slang::ComPtr<slang::IBlob> diagnostics;
    auto *module_ptr = device->getSlangSession()->loadModuleFromIRBlob(
        desc.name, desc.name, blob.get(), diagnostics.writeRef());
    diagnose_if_needed(diagnostics.get());
    return Slang::ComPtr<slang::IModule>(module_ptr);
}

} // namespace llc
