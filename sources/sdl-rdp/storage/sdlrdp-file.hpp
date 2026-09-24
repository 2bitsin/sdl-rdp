#pragma once
#include <sdl-rdp/storage/drive-packet.hpp>

#include <memory>
#include <string>

namespace Backend {
class DriveChannel;
}
struct sdlrdp_file {
public:
       sdlrdp_file(sdlrdp_file const&)               = delete;
       sdlrdp_file(sdlrdp_file&&)                    = delete;
       sdlrdp_file(std::shared_ptr<Backend::DriveChannel> source, unsigned device, unsigned file, std::string name);
       ~sdlrdp_file();
  auto operator=(sdlrdp_file const&) -> sdlrdp_file& = delete;
  auto operator=(sdlrdp_file&&)      -> sdlrdp_file& = delete;
  auto Close()                       -> void;
  auto Channel() const               -> std::shared_ptr<Backend::DriveChannel> const&;
  auto Drive() const                 -> unsigned;
  auto Id() const                    -> unsigned;
  auto Path() const                  -> std::string const&;

private:
  std::shared_ptr<Backend::DriveChannel> channel;
  unsigned                               drive;
  unsigned                               wire;
  std::string                            path;
  bool                                   closed { };
};
namespace Backend {
auto Exchange(sdlrdp_file& file, unsigned major, DrivePacket const& packet, unsigned minor = 0, bool end = false)
    -> DrivePacket;
}
