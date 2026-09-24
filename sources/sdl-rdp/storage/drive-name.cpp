#include <sdl-rdp/storage/drive-channel.hpp>
#include <sdl-rdp/utilities/transcode.hpp>

#include <freerdp/channels/rdpdr.h>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>

namespace Backend {
namespace {
auto DecodeDriveName(std::span<std::byte const> bytes, std::uint32_t drive_version, char const* dos) -> std::string {
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
  return dos;
}
}
auto DriveChannel::Name(std::span<std::byte const> bytes, char const* dos) const -> std::string {
  std::string label(dos);
  try {
    label = DecodeDriveName(bytes, drive_version, dos);
  } catch (std::exception const& error) {
    label = dos;
    Warn(std::format("{} Using DOS name '{}'.", error.what(), dos));
  }
  constexpr std::size_t capacity = sizeof(sdlrdp_drive::name) - 1;
  if (label.size() > capacity) {
    Warn(std::format("Drive name exceeds {} bytes; truncating.", capacity));
    auto end = capacity;
    while ((std::bit_cast<std::uint8_t>(label[end]) & 0xc0) == 0x80) --end;
    label.resize(end);
  }
  return label;
}
}
