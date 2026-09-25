#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sdl_rdp::clipboard::detail::text {
auto ClipboardAnsi(std::string_view text)            -> std::string;
auto ClipboardUnicode(std::string_view text)         -> std::vector<std::byte>;
auto ClipboardUtf8(std::span<std::byte const> bytes) -> std::string;
}

namespace sdl_rdp::clipboard {
using detail::text::ClipboardAnsi;
using detail::text::ClipboardUnicode;
using detail::text::ClipboardUtf8;
}
