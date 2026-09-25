#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <cstdint>

namespace sdl_rdp::video::detail::exceptions {
using ::Backend::operator""_hash;
using ::Backend::ArgumentFailure;

using InvalidPointerLayout = ArgumentFailure<"InvalidPointerLayout"_hash,
                                             "Pointer {}x{} with hotspot {},{} is invalid.", std::uint32_t,
                                             std::uint32_t, std::uint32_t, std::uint32_t>;
}
namespace sdl_rdp::video {
using detail::exceptions::InvalidPointerLayout;
}
