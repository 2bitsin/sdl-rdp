#include <sdl-rdp/drive/label.hpp>
#include <sdl-rdp/utilities/transcode.hpp>

#include <freerdp/channels/rdpdr.h>
#include <bit>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sdl_rdp::drive::detail::label {
using Backend::TranscodeRange;
auto DecodeLabel(std::span<std::byte const> bytes, std::uint32_t drive_version, std::string_view dos) -> std::string {
  if (!bytes.empty()) {
    if (bytes.back() != std::byte{ }) throw std::runtime_error("Unterminated drive name.");
    // FreeRDP 3.32 drive_main.c:1023 sends UTF-8 despite advertising drive capability v2.
    bool const wide   = drive_version >= DRIVE_CAPABILITY_VERSION_02 && bytes.size() >= 2 && bytes.size() % 2 == 0
                        && bytes[bytes.size() - 2] == std::byte{ };
    auto       format = wide ? oxbox::utilities::TextFormat{ .encoding = oxbox::utilities::Encoding::UTF16,
                                                             .order    = std::endian::little }
                             : oxbox::utilities::TextFormat{ };
    auto       label  = TranscodeRange<std::string>(bytes.first(bytes.size() - (wide ? 2 : 1)), format, { });
    if (label.find('\0') != std::string::npos) throw std::runtime_error("Embedded null in drive name.");
    return label;
  }
  return std::string{ dos };
}
}
