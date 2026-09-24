#pragma once
#include "pinned.hpp"
#include "pixel-band.hpp"
#include "sdl-rdp-backend.h"

#include <freerdp/update.h>
#include <optional>
#include <span>
#include <vector>

namespace Backend {
class Activation;
class Configuration;
class Encoder;
class FramePacing;
class PeerFrames;
class PeerLink;
class Scaler;
enum class LegacyWire{ Bitmap, Planar, Surface };
struct LegacyFormat {
  unsigned   depth{ 32                 };
  unsigned   codec{ };
  LegacyWire wire { LegacyWire::Bitmap };
};
class LegacyFrame : private Pinned {
public:
       LegacyFrame(PeerLink& link, Configuration const& configuration, Activation& activation, PeerFrames& frames,
                   FramePacing& pacing, Encoder& encoder, Scaler& scaler) noexcept;
  bool Prepare();
  bool Encode();
  bool Send();
  bool Delivered() const noexcept;

private:
  struct Band {
    sdlrdp_rect       area  { };
    std::vector<BYTE> pixels;
  };
  struct Packet {
    std::vector<Band>        bands;
    std::vector<BITMAP_DATA> rectangles;
  };
  bool SelectEncoder();
  void AppendPlanar(Packet& packet, std::size_t& wire_size, sdlrdp_rect area, std::span<BYTE const> payload);
  bool AppendBand(PixelBand band);
  bool Finish();
  bool Marker(UINT16 action);
  bool Planar(sdlrdp_rect area);
  bool Bands(sdlrdp_rect area);
  void Describe(Packet& packet) const;
  bool Write(Packet& packet);
  PeerLink&                  _link;
  Configuration const&       _configuration;
  Activation&                _activation;
  PeerFrames&                _frames;
  FramePacing&               _pacing;
  Encoder&                   _encoder;
  Scaler&                    _scaler;
  std::vector<Packet>        _packets;
  std::optional<std::size_t> _next;
  LegacyFormat               _format       { };
};
}
