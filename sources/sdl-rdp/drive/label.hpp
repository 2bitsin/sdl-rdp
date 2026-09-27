#pragma once
#include <sdl-rdp/freerdp-facade/rdpdr.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sdl_rdp::drive::detail::label {
using sdl_rdp::freerdp_facade::CapabilityVersion;
auto DecodeLabel(std::span<std::byte const> bytes, std::optional<CapabilityVersion> drive_version, std::string_view dos)
    -> std::string;
}

namespace sdl_rdp::drive {
using detail::label::DecodeLabel;
}
