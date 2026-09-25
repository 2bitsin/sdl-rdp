#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <cstdint>

namespace sdl_rdp::picture::detail::exceptions {
using ::Backend::operator""_hash;
using ::Backend::RuntimeFailure;

using DesktopExceedsLimits = RuntimeFailure<"DesktopExceedsLimits"_hash,
                                            "Aspect-corrected desktop {}x{} exceeds RDP dimensions.", std::uint64_t,
                                            std::uint64_t>;
}
namespace sdl_rdp::picture {
using detail::exceptions::DesktopExceedsLimits;
}
