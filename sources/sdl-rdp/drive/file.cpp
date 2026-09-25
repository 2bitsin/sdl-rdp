#include <sdl-rdp/drive/file.hpp>
#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <freerdp/channels/rdpdr.h>
#include <cstddef>
#include <cstdint>
#include <format>
#include <utility>

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
  sdl_rdp::drive::Exchange(*this, IRP_MJ_CLOSE, packet);
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
namespace sdl_rdp::drive::detail::file {
auto Exchange(sdlrdp_file& file, std::uint32_t major, DrivePacket const& packet, std::uint32_t minor, bool end)
    -> DrivePacket {
  utilities::Expects(file.Channel() != nullptr, "file retains its channel");
  auto request = file.Channel()->Send(file.Drive(), file.Id(), major, packet, minor);
  return file.Channel()->Wait(request, file.Path(), end);
}
}
