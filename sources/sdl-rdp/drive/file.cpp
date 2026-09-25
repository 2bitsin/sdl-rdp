#include <sdl-rdp/drive/file.hpp>

#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/drive/information.hpp>
#include <sdl-rdp/drive/transfer.hpp>
#include <sdl-rdp/freerdp-facade/rdpdr.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace sdl_rdp::drive::detail::file {
using sdl_rdp::freerdp_facade::InformationClass;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::InvalidArguments;

auto Exchange(File& file, IrpMajor major, DrivePacket const& packet, IrpMinor minor, bool end) -> DrivePacket {
  Expects(file.Channel() != nullptr, "file retains its channel");
  auto request = file.Channel()->Send(file.Drive(), file.Id(), major, packet, minor);
  return file.Channel()->Wait(request, file.Path(), end);
}
namespace {
auto QueryInformation(File& file, InformationClass type) -> DrivePacket {
  return Information(Exchange(file, IrpMajor::QueryInformation, InformationRequest(type, { })));
}
}
File::File(std::shared_ptr<DriveChannel> source, std::uint32_t device, std::uint32_t file, std::string name)
    : _channel{ std::move(source) }, _drive{ device }, _wire{ file }, _path{ std::move(name) } { }
File::~File() {
  std::ignore = Contained(
      [this] { Close(); },
      [this](std::string_view cause) { _channel->Warn(std::format("Drive close '{}': {}", _path, cause)); });
}
auto File::Close() -> void {
  if (std::exchange(_closed, true)) return;
  DrivePacket           packet;
  constexpr std::size_t padding_after_request_header = 32;
  packet.Zero(padding_after_request_header);
  Exchange(*this, IrpMajor::Close, packet);
}
auto File::Channel() const -> std::shared_ptr<DriveChannel> const& {
  return _channel;
}
auto File::Drive() const -> std::uint32_t {
  return _drive;
}
auto File::Id() const -> std::uint32_t {
  return _wire;
}
auto File::Path() const -> std::string const& {
  return _path;
}
template <class ByteTy> auto File::Checked(std::uint64_t offset, std::span<ByteTy> bytes) -> std::size_t {
  static_assert(std::same_as<std::remove_const_t<ByteTy>, std::byte>);
  Expects(!_closed, "the file is open");
  auto const room = std::numeric_limits<std::uint64_t>::max() - bytes.size();
  if (offset > room) throw InvalidArguments{ "drive transfer", "size or offset" };
  return sdl_rdp::drive::Transfer(*this, offset, bytes);
}
auto File::Transfer(std::uint64_t offset, std::span<std::byte> bytes) -> std::size_t {
  return Checked(offset, bytes);
}
auto File::Transfer(std::uint64_t offset, std::span<std::byte const> bytes) -> std::size_t {
  return Checked(offset, bytes);
}
auto File::Stat() -> FileStatus {
  Expects(!_closed, "the file is open");
  auto const basic = Basic(QueryInformation(*this, InformationClass::Basic));
  return { .size      = EndOfFile(QueryInformation(*this, InformationClass::Standard)),
           .directory = basic.directory,
           .modified  = basic.modified };
}
}
