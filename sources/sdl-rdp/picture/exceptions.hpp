#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::picture::detail::exceptions {
using oxbox::utilities::literals::operator""_hash;
using sdl_rdp::utilities::ArgumentFailure;
using sdl_rdp::utilities::RuntimeFailure;

using DesktopExceedsLimits = RuntimeFailure<"DesktopExceedsLimits"_hash,
                                            "Aspect-corrected desktop {}x{} exceeds RDP dimensions.", std::uint64_t,
                                            std::uint64_t>;
using ShortPitch = ArgumentFailure<"ShortPitch"_hash, "Pitch {} is shorter than a {}-byte row.", int, std::size_t>;
using DamageOutOfBounds = ArgumentFailure<"DamageOutOfBounds"_hash, "Present failed: damage exceeds the frame.">;
}

namespace sdl_rdp::picture {
using detail::exceptions::DamageOutOfBounds;
using detail::exceptions::DesktopExceedsLimits;
using detail::exceptions::ShortPitch;
}
