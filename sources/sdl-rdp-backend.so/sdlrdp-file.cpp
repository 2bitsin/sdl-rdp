#include "_detail/sdlrdp-file.hpp"
#include "_detail/contract.hpp"
#include "_detail/drive-channel.hpp"

#include <format>
#include <freerdp/channels/rdpdr.h>
#include <utility>

sdlrdp_file::sdlrdp_file(std::shared_ptr<Backend::DriveChannel> source, unsigned device, unsigned file,
                         std::string name)
    : channel { std::move(source) }, drive{ device }, wire{ file }, path{ std::move(name) } { }
sdlrdp_file::~sdlrdp_file() {
  try {
    Close();
  } catch (std::exception const& error) {
    channel->Warn(std::format("Drive close '{}': {}", path, error.what()));
  }
}
void sdlrdp_file::Close() {
  if (std::exchange(closed, true)) return;
  Backend::DrivePacket packet;
  constexpr unsigned   padding_after_request_header = 32;
  packet.Zero(padding_after_request_header);
  Backend::Exchange(*this, IRP_MJ_CLOSE, packet);
}
std::shared_ptr<Backend::DriveChannel> const& sdlrdp_file::Channel() const { return channel; }
unsigned sdlrdp_file::Drive() const { return drive; }
unsigned sdlrdp_file::Id() const { return wire; }
std::string const& sdlrdp_file::Path() const { return path; }
namespace Backend {
DrivePacket Exchange(sdlrdp_file& file, unsigned major, DrivePacket const& packet, unsigned minor, bool end) {
  utilities::Expects(file.Channel() != nullptr, "file retains its channel");
  auto request = file.Channel()->Send(file.Drive(), file.Id(), major, packet, minor);
  return file.Channel()->Wait(request, file.Path(), end);
}
}
