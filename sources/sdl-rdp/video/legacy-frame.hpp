#pragma once
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/pixel-band.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

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
  auto Prepare()                  -> bool;
  auto Encode()                   -> bool;
  auto Send()                     -> bool;
  auto Delivered() const noexcept -> bool;

private:
  struct Band {
    sdlrdp_rect       area  { };
    std::vector<BYTE> pixels;
  };
  struct Packet {
    std::vector<Band>        bands;
    std::vector<BITMAP_DATA> rectangles;
  };
  auto SelectEncoder()                                                                                       -> bool;
  auto AppendPlanar(Packet& packet, std::size_t& wire_size, sdlrdp_rect area, std::span<BYTE const> payload) -> void;
  auto AppendBand(PixelBand band)                                                                            -> bool;
  auto Finish()                                                                                              -> bool;
  auto Marker(UINT16 action)                                                                                 -> bool;
  auto Planar(sdlrdp_rect area)                                                                              -> bool;
  auto Bands(sdlrdp_rect area)                                                                               -> bool;
  auto Describe(Packet& packet) const                                                                        -> void;
  auto Write(Packet& packet)                                                                                 -> bool;
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
