#pragma once
#include <oxbox/utilities/serdes.hpp>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Backend {
class DriveChannel;
template <typename _Value>
concept WireField = std::unsigned_integral<_Value> && !std::same_as<_Value, bool>;
class DrivePacket {
public:
  [[noreturn]] auto                Invalid(std::string const& cause) const     -> void;
  template <WireField _Value> auto Read()                                      -> _Value;
  template <WireField _Value> auto Write(_Value value)                         -> void;
  auto                             Zero(std::size_t count)                     -> void;
  auto                             Append(std::span<std::byte const> data)     -> void;
  auto                             Skip(std::size_t count)                     -> void;
  auto                             Text(std::size_t count)                     -> std::string;
  auto                             Bytes()                                     -> std::vector<std::byte>&;
  auto                             Bytes() const                               -> std::vector<std::byte> const&;
  auto                             Position() const                            -> std::size_t;
  auto                             Seek(std::size_t offset)                    -> void;
  auto                             Origin(std::weak_ptr<DriveChannel> channel) -> void;

private:
  using Writer = oxbox::utilities::GrowingWriter<std::endian::little>;
  auto Remaining() const -> oxbox::utilities::BoundedReader;
  auto Consumed(oxbox::utilities::BoundedReader const& reader, std::string const& cause) -> void;
  std::weak_ptr<DriveChannel> origin;
  std::vector<std::byte>      bytes;
  std::size_t                 position{ };
};
auto DrivePath(char const* path) -> std::vector<std::byte>;
template <WireField _Value> auto DrivePacket::Read() -> _Value {
  auto       reader = Remaining();
  auto const value  = reader.Fetch<_Value, std::endian::little>();
  Consumed(reader, "Truncated drive response.");
  return value;
}
template <WireField _Value> auto DrivePacket::Write(_Value value) -> void {
  Writer{ bytes }.Put(value);
}
}
