#pragma once
#include "drive-packet.hpp"

#include <memory>
#include <string>

namespace Backend {
class DriveChannel;
}
struct sdlrdp_file {
public:
                                                sdlrdp_file(sdlrdp_file const&) = delete;
                                                sdlrdp_file(sdlrdp_file&&)      = delete;
  sdlrdp_file(std::shared_ptr<Backend::DriveChannel> source, unsigned device, unsigned file, std::string name);
                                                ~sdlrdp_file();
  sdlrdp_file&                                  operator = (sdlrdp_file const&) = delete;
  sdlrdp_file&                                  operator = (sdlrdp_file&&)      = delete;
  void                                          Close();
  std::shared_ptr<Backend::DriveChannel> const& Channel() const;
  unsigned                                      Drive() const;
  unsigned                                      Id() const;
  std::string const&                            Path() const;

private:
  std::shared_ptr<Backend::DriveChannel> channel;
  unsigned                               drive;
  unsigned                               wire;
  std::string                            path;
  bool                                   closed { };
};
namespace Backend {
DrivePacket Exchange(sdlrdp_file& file, unsigned major, DrivePacket const& packet, unsigned minor = 0,
                     bool end = false);
}
