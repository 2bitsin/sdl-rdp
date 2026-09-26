#pragma once
#include <oxbox/utilities/serdes.hpp>
#include <sdl-rdp/drive/forward.hpp>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sdl_rdp::drive::detail::packet {
using ChannelOrigin = std::weak_ptr<channel::DriveChannel>;
template <typename ValueTy>
concept WireField = std::unsigned_integral<ValueTy> && !std::same_as<ValueTy, bool>;
class DrivePacket {
public:
  [[noreturn]] auto                 Invalid(std::string_view cause) const          -> void;
  template <WireField ValueTy> auto Read()                                         -> ValueTy;
  template <WireField ValueTy> auto Write(ValueTy value)                           -> void;
  auto                              Zero(std::size_t count)                        -> void;
  auto                              Append(std::span<std::byte const> data)        -> void;
  auto                              AppendCounted(std::span<std::byte const> data) -> void;
  auto                              Skip(std::size_t count)                        -> void;
  auto                              Text(std::size_t count)                        -> std::string;
  auto                              Bytes()                                        -> std::vector<std::byte>&;
  auto                              Bytes() const                                  -> std::vector<std::byte> const&;
  auto                              Position() const                               -> std::size_t;
  auto                              Seek(std::size_t offset)                       -> void;
  auto                              Origin(ChannelOrigin channel)                  -> void;

private:
  using Writer = oxbox::utilities::GrowingWriter<std::endian::little>;
  auto Remaining() const -> oxbox::utilities::BoundedReader;
  auto Consumed(oxbox::utilities::BoundedReader const& reader, std::string_view cause) -> void;
  ChannelOrigin          origin;
  std::vector<std::byte> bytes;
  std::size_t            position{ };
};
auto DrivePath(std::string_view path) -> std::vector<std::byte>;
template <WireField ValueTy> auto DrivePacket::Read() -> ValueTy {
  auto       reader = Remaining();
  auto const value  = reader.Fetch<ValueTy, std::endian::little>();
  Consumed(reader, "truncated");
  return value;
}
template <WireField ValueTy> auto DrivePacket::Write(ValueTy value) -> void {
  Writer{ bytes }.Put(value);
}
}

namespace sdl_rdp::drive {
using detail::packet::DrivePacket;
using detail::packet::DrivePath;
using detail::packet::WireField;
}
