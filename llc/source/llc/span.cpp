#include "span.h"

#include <llc/utils/embedded_module.h>

namespace llc {

namespace {

extern "C" const unsigned char _binary_span_slang_module_start[]; // NOLINT(readability-identifier-naming)
extern "C" const unsigned char _binary_span_slang_module_end[];   // NOLINT(readability-identifier-naming)

} // namespace

Slang::ComPtr<slang::IModule> load_span_module(slang::ISession *session) {
    return load_embedded_module(session, EmbededModuleDesc{
                                             .name = "span",
                                             .start = _binary_span_slang_module_start,
                                             .end = _binary_span_slang_module_end,
                                         });
}

Slang::ComPtr<slang::IModule> load_span_module(rhi::IDevice *device) {
    return load_span_module(device->getSlangSession());
}
} // namespace llc