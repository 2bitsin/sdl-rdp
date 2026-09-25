#include <sdl-rdp/drive/file.hpp>
#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/drive/information.hpp>
#include <sdl-rdp/drive/transfer.hpp>
#include <sdl-rdp/freerdp-facade/rdpdr.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <type_traits>
#include <utility>

namespace {
auto QueryInformation(sdlrdp_file& file, sdl_rdp::freerdp_facade::InformationClass type)
    -> sdl_rdp::drive::DrivePacket {
  using namespace sdl_rdp::drive;
  using sdl_rdp::freerdp_facade::IrpMajor;
  return Information(Exchange(file, IrpMajor::QueryInformation, InformationRequest(type, { })));
}
}

sdlrdp_file::sdlrdp_file(std::shared_ptr<sdl_rdp::drive::DriveChannel> source, std::uint32_t device, std::uint32_t file,
                         std::string name)
    : channel{ std::move(source) }, drive{ device }, wire{ file }, path{ std::move(name) } { }
sdlrdp_file::~sdlrdp_file() {
  try {
    Close();
  } catch (std::exception const& error) {
    channel->Warn(std::format("Drive close '{}': {}", path, error.what()));
  }
}
auto sdlrdp_file::Close() -> void {
  if (std::exchange(closed, true)) return;
  sdl_rdp::drive::DrivePacket packet;
  constexpr std::size_t       padding_after_request_header = 32;
  packet.Zero(padding_after_request_header);
  sdl_rdp::drive::Exchange(*this, sdl_rdp::freerdp_facade::IrpMajor::Close, packet);
}
auto sdlrdp_file::Channel() const -> std::shared_ptr<sdl_rdp::drive::DriveChannel> const& {
  return channel;
}
auto sdlrdp_file::Drive() const -> std::uint32_t {
  return drive;
}
auto sdlrdp_file::Id() const -> std::uint32_t {
  return wire;
}
auto sdlrdp_file::Path() const -> std::string const& {
  return path;
}
template <class ByteTy> auto sdlrdp_file::Checked(std::uint64_t offset, std::span<ByteTy> bytes) -> int {
  static_assert(std::same_as<std::remove_const_t<ByteTy>, std::byte>);
  utilities::Expects(!closed, "the file is open");
  auto const room = std::numeric_limits<std::uint64_t>::max() - bytes.size();
  if (std::cmp_greater(bytes.size(), std::numeric_limits<int>::max()) || offset > room)
    throw Backend::InvalidArguments{ "drive transfer", "size or offset" };
  return sdl_rdp::drive::Transfer(*this, offset, bytes);
}
auto sdlrdp_file::Transfer(std::uint64_t offset, std::span<std::byte> bytes) -> int {
  return Checked(offset, bytes);
}
auto sdlrdp_file::Transfer(std::uint64_t offset, std::span<std::byte const> bytes) -> int {
  return Checked(offset, bytes);
}
auto sdlrdp_file::Stat() -> sdlrdp_stat {
  utilities::Expects(!closed, "the file is open");
  using namespace sdl_rdp;
  auto const basic = drive::Basic(QueryInformation(*this, freerdp_facade::InformationClass::Basic));
  return { .size      = drive::EndOfFile(QueryInformation(*this, freerdp_facade::InformationClass::Standard)),
           .directory = int{ basic.directory },
           .modified  = basic.modified };
}
namespace sdl_rdp::drive::detail::file {
auto Exchange(sdlrdp_file& file, freerdp_facade::IrpMajor major, DrivePacket const& packet,
              freerdp_facade::IrpMinor minor, bool end) -> DrivePacket {
  utilities::Expects(file.Channel() != nullptr, "file retains its channel");
  auto request = file.Channel()->Send(file.Drive(), file.Id(), major, packet, minor);
  return file.Channel()->Wait(request, file.Path(), end);
}
}
