#include <sdl-rdp/storage/drive-packet.hpp>
#include <sdl-rdp/storage/malformed-response.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/transcode.hpp>

#include <stdexcept>
#include <utility>

namespace Backend {
auto DrivePacket::Invalid(std::string const& cause) const -> void {
  throw MalformedResponse(cause, origin);
}
auto DrivePacket::Remaining() const -> oxbox::utilities::BoundedReader {
  utilities::Expects(position <= bytes.size(), "packet cursor is bounded");
  return oxbox::utilities::BoundedReader{ std::span(bytes).subspan(position) };
}
auto DrivePacket::Consumed(oxbox::utilities::BoundedReader const& reader, std::string const& cause) -> void {
  if (!reader.Sound()) Invalid(cause);
  position += reader.At();
}
auto DrivePacket::Zero(std::size_t count) -> void {
  Writer{ bytes }.Zero(count);
}
auto DrivePacket::Append(std::span<std::byte const> data) -> void {
  Writer{ bytes }.Append(data);
}
auto DrivePacket::Skip(std::size_t count) -> void {
  auto reader = Remaining();
  std::ignore = reader.Take(count);
  Consumed(reader, "Truncated drive response.");
}
auto DrivePacket::Text(std::size_t count) -> std::string {
  auto       reader = Remaining();
  auto const taken  = reader.Take(count);
  Consumed(reader, "Truncated drive name.");
  try {
    return TranscodeRange<std::string>(taken, Utf16Little, { });
  } catch (std::exception const& error) {
    Invalid(error.what());
  }
}
auto DrivePacket::Bytes() -> std::vector<std::byte>& {
  return bytes;
}
auto DrivePacket::Bytes() const -> std::vector<std::byte> const& {
  return bytes;
}
auto DrivePacket::Position() const -> std::size_t {
  return position;
}
auto DrivePacket::Seek(std::size_t offset) -> void {
  utilities::Expects(offset <= bytes.size(), "packet cursor is bounded");
  position = offset;
}
auto DrivePacket::Origin(std::weak_ptr<DriveChannel> channel) -> void {
  origin = std::move(channel);
}
auto DrivePath(char const* path) -> std::vector<std::byte> {
  if (!path) throw std::runtime_error("Drive path is null.");
  std::string text(path);
  if (text.empty() || text.front() != '/') text.insert(text.begin(), '/');
  auto encoded = TranscodeRange<std::vector<std::byte>>(std::as_bytes(std::span(text)), { }, Utf16Little,
                                                        [](char32_t point) { return point == U'/' ? U'\\' : point; });
  encoded.resize(encoded.size() + 2);
  return encoded;
}
}
