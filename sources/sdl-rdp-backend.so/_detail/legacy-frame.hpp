#pragma once
#include "sdl-rdp-backend.h"
#include <freerdp/update.h>
#include <optional>
#include <vector>

namespace Backend {
class Peer;
class LegacyFrame {
public:
  bool Prepare(Peer& peer);
  bool Encode(Peer& peer);
  bool Send(Peer& peer);
private:
  struct Band {
    sdlrdp_rect       area   {};
    std::vector<BYTE> pixels {};
  };
  struct Packet {
    std::vector<Band>        bands      {};
    std::vector<BITMAP_DATA> rectangles {};
  };
  bool Planar(Peer& peer, sdlrdp_rect area);
  bool Bands(Peer& peer, sdlrdp_rect area);
  void Describe(Packet& packet);
  bool Write(rdpUpdate* update, Packet& packet);
  enum class Wire { Bitmap, Planar, Surface };
  std::vector<Packet>        packets {               };
  std::optional<std::size_t> next    {               };
  unsigned                   depth   { 32            };
  unsigned                   codec   { 0             };
  Wire                       wire    { Wire::Bitmap  };
};
}
