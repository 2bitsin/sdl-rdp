#include "_detail/drive-packet.hpp"
#include "_detail/contract.hpp"
#include "_detail/malformed-response.hpp"
#include "_detail/transcode.hpp"

#include <stdexcept>
#include <utility>

namespace Backend {
namespace {
auto RequireRemaining(DrivePacket const& packet, size_t count, std::string const& cause) -> void {
  utilities::Expects(packet.Position() <= packet.Bytes().size(), "packet cursor is bounded");
  if (count > packet.Bytes().size() - packet.Position()) packet.Invalid(cause);
}
}
auto DrivePacket::Invalid(std::string const& cause) const -> void { throw MalformedResponse(cause, origin); }
auto DrivePacket::Get(unsigned count) -> uint64_t {
  utilities::Expects(count <= 8, "integer fits uint64");
  RequireRemaining(*this, count, "Truncated drive response.");
  uint64_t value{ };
  for (unsigned i = 0; i < count; ++i)
    value |= uint64_t(bytes[position++]) << (i * 8);
  return value;
}
auto DrivePacket::Put(uint64_t value, unsigned count) -> void {
  utilities::Expects(count <= 8, "integer fits uint64");
  for (unsigned i = 0; i < count; ++i)
    bytes.push_back(uint8_t(value >> (i * 8)));
}
auto DrivePacket::Zero(size_t count) -> void { bytes.resize(bytes.size() + count); }
auto DrivePacket::Append(std::span<uint8_t const> data) -> void { bytes.insert(bytes.end(), data.begin(), data.end()); }
auto DrivePacket::Skip(size_t count) -> void {
  RequireRemaining(*this, count, "Truncated drive response.");
  position += count;
}
auto DrivePacket::Text(size_t count) -> std::string {
  RequireRemaining(*this, count, "Truncated drive name.");
  try {
    auto result = TranscodeRange<std::string>(
        std::as_bytes(std::span(bytes).subspan(position, count)),
        Utf16Little, { });
    position += count;
    return result;
  } catch (std::exception const& error) {
    Invalid(error.what());
  }
}
auto DrivePacket::Bytes() -> std::vector<uint8_t>& { return bytes; }
auto DrivePacket::Bytes() const -> std::vector<uint8_t> const& { return bytes; }
auto DrivePacket::Position() const -> size_t { return position; }
auto DrivePacket::Seek(size_t offset) -> void {
  utilities::Expects(offset <= bytes.size(), "packet cursor is bounded");
  position = offset;
}
auto DrivePacket::Origin(std::weak_ptr<DriveChannel> channel) -> void { origin = std::move(channel); }
auto DrivePath(char const* path) -> std::vector<uint8_t> {
  if (!path) throw std::runtime_error("Drive path is null.");
  std::string text(path);
  if (text.empty() || text.front() != '/') text.insert(text.begin(), '/');
  auto encoded = TranscodeRange<std::vector<uint8_t>>(
      std::as_bytes(std::span(text)), { },
      Utf16Little,
      [](char32_t point) { return point == U'/' ? U'\\' : point; });
  encoded.resize(encoded.size() + 2);
  return encoded;
}
}
