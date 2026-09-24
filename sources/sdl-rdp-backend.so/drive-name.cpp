#include "_detail/state.hpp"

#include <freerdp/channels/rdpdr.h>

namespace Backend {
namespace {
std::string DecodeDriveName(std::span<uint8_t const> bytes, unsigned drive_version, char const* dos) {
  if (!bytes.empty()) {
    if (bytes.back()) throw std::runtime_error("Unterminated drive name.");
    // FreeRDP 3.15 sends UTF-8 despite advertising drive capability v2.
    bool const wide   = drive_version >= DRIVE_CAPABILITY_VERSION_02 && bytes.size() >= 2 && bytes.size() % 2 == 0 &&
                        bytes[bytes.size() - 2] == 0;
    auto       format = wide ? oxbox::utilities::TextFormat{ .encoding = oxbox::utilities::Encoding::UTF16,
                                                             .order    = std::endian::little }
                             : oxbox::utilities::TextFormat{ };
    auto label = TranscodeRange<std::string>(std::as_bytes(bytes.first(bytes.size() - (wide ? 2 : 1))), format, { });
    if (label.find('\0') != std::string::npos) throw std::runtime_error("Embedded null in drive name.");
    return label;
  }
  return dos;
}
}
std::string DriveChannel::Name(std::span<uint8_t const> bytes, char const* dos) const {
  std::string label(dos);
  try {
    label = DecodeDriveName(bytes, drive_version, dos);
  } catch (std::exception const& error) {
    label = dos;
    Warn(std::format("{} Using DOS name '{}'.", error.what(), dos));
  }
  constexpr size_t capacity = sizeof(sdlrdp_drive::name) - 1;
  if (label.size() > capacity) {
    Warn(std::format("Drive name exceeds {} bytes; truncating.", capacity));
    auto end = capacity;
    while ((uint8_t(label[end]) & 0xc0) == 0x80)
      --end;
    label.resize(end);
  }
  return label;
}
}
