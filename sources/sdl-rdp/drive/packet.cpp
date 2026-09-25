#include <sdl-rdp/drive/packet.hpp>

#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/transcode.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace sdl_rdp::drive::detail::packet {
using Backend::InvalidEncoding;
using Backend::NullArgument;
using Backend::Reported;
using Backend::TranscodeRange;
using Backend::Utf16Little;
// A malformed response leaves its channel out of step with the client, so the channel is aborted before the failure.
auto DrivePacket::Invalid(std::string_view cause) const -> void {
  throw Reported(MalformedResponse{ cause }, [this](std::string_view text) {
    if (auto const channel = origin.lock()) channel->Abort(std::string{ text });
  });
}
auto DrivePacket::Remaining() const -> oxbox::utilities::BoundedReader {
  utilities::Expects(position <= bytes.size(), "packet cursor is bounded");
  return oxbox::utilities::BoundedReader{ std::span(bytes).subspan(position) };
}
auto DrivePacket::Consumed(oxbox::utilities::BoundedReader const& reader, std::string_view cause) -> void {
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
  Consumed(reader, "truncated");
}
auto DrivePacket::Text(std::size_t count) -> std::string {
  auto       reader = Remaining();
  auto const taken  = reader.Take(count);
  Consumed(reader, "truncated name");
  try {
    return TranscodeRange<std::string>(taken, Utf16Little, { });
  } catch (InvalidEncoding const&) {
    Invalid("undecodable name");
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
auto DrivePacket::Origin(ChannelOrigin channel) -> void {
  origin = std::move(channel);
}
auto DrivePath(char const* path) -> std::vector<std::byte> {
  if (!path) throw NullArgument{ "Drive path" };
  std::string text(path);
  if (text.empty() || text.front() != '/') text.insert(text.begin(), '/');
  auto encoded = TranscodeRange<std::vector<std::byte>>(std::as_bytes(std::span(text)), { }, Utf16Little,
                                                        [](char32_t point) { return point == U'/' ? U'\\' : point; });
  encoded.resize(encoded.size() + 2);
  return encoded;
}
}
