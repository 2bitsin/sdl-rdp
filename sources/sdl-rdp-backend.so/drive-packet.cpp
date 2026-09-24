#include "_detail/drive-packet.hpp"
#include "_detail/contract.hpp"
#include "_detail/malformed-response.hpp"
#include "_detail/transcode.hpp"

#include <stdexcept>
#include <utility>

namespace Backend {
namespace {
void RequireRemaining(DrivePacket const& packet, size_t count, std::string const& cause) {
  utilities::Expects(packet.Position() <= packet.Bytes().size(), "packet cursor is bounded");
  if (count > packet.Bytes().size() - packet.Position()) packet.Invalid(cause);
}
}
void DrivePacket::Invalid(std::string const& cause) const { throw MalformedResponse(cause, origin); }
uint64_t DrivePacket::Get(unsigned count) {
  utilities::Expects(count <= 8, "integer fits uint64");
  RequireRemaining(*this, count, "Truncated drive response.");
  uint64_t value{ };
  for (unsigned i = 0; i < count; ++i)
    value |= uint64_t(bytes[position++]) << (i * 8);
  return value;
}
void DrivePacket::Put(uint64_t value, unsigned count) {
  utilities::Expects(count <= 8, "integer fits uint64");
  for (unsigned i = 0; i < count; ++i)
    bytes.push_back(uint8_t(value >> (i * 8)));
}
void DrivePacket::Zero(size_t count) { bytes.resize(bytes.size() + count); }
void DrivePacket::Append(std::span<uint8_t const> data) { bytes.insert(bytes.end(), data.begin(), data.end()); }
void DrivePacket::Skip(size_t count) {
  RequireRemaining(*this, count, "Truncated drive response.");
  position += count;
}
std::string DrivePacket::Text(size_t count) {
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
std::vector<uint8_t>& DrivePacket::Bytes() { return bytes; }
std::vector<uint8_t> const& DrivePacket::Bytes() const { return bytes; }
size_t DrivePacket::Position() const { return position; }
void DrivePacket::Seek(size_t offset) {
  utilities::Expects(offset <= bytes.size(), "packet cursor is bounded");
  position = offset;
}
void DrivePacket::Origin(std::weak_ptr<DriveChannel> channel) { origin = std::move(channel); }
std::vector<uint8_t> DrivePath(char const* path) {
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
