#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace sdl_rdp::drive::detail::label {
auto DecodeLabel(std::span<std::byte const> bytes, std::uint32_t drive_version, std::string_view dos) -> std::string;
}

namespace sdl_rdp::drive {
using detail::label::DecodeLabel;
}
