#pragma once

#include <slang-rhi.h>
#include <llc/types.hpp>

namespace llc {

struct EmbededModuleDesc final {
    const char *name;
    const u8 *start;
    const u8 *end;
};

/// Loads a precompiled Slang IR module from embedded binary data into the given Slang session.
Slang::ComPtr<slang::IModule> load_embedded_module(slang::ISession *session, EmbededModuleDesc const &desc);

/// Loads a precompiled Slang IR module from embedded binary data into the device's Slang session.
/// The module is registered by name so subsequent `import` statements can resolve it.
Slang::ComPtr<slang::IModule> load_embedded_module(rhi::IDevice *device, EmbededModuleDesc const &desc);

} // namespace llc
