#pragma once
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/pixel-band.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <freerdp/update.h>
#include <cstddef>
#include <cstdint>
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
  std::uint32_t depth{ 32                 };
  std::uint32_t codec{ };
  LegacyWire    wire { LegacyWire::Bitmap };
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
    sdlrdp_rect            area { };
    std::vector<std::byte> bytes;
  };
  struct Packet {
    std::vector<Band>        bands;
    std::vector<BITMAP_DATA> rectangles;
  };
  struct Queue {
    std::vector<Packet>        packets;
    std::optional<std::size_t> next;
  };
  auto SelectEncoder()                -> bool;
  auto AppendPlanar(Packet& packet, std::size_t& wire_size, sdlrdp_rect area, std::span<std::byte const> payload)
      -> void;
  auto AppendBand(PixelBand band)     -> bool;
  auto Finish()                       -> bool;
  auto Marker(std::uint16_t action)   -> bool;
  auto Planar(sdlrdp_rect area)       -> bool;
  auto Bands(sdlrdp_rect area)        -> bool;
  auto Describe(Packet& packet) const -> void;
  auto Write(Packet& packet)          -> bool;
  PeerLink&                 _link;
  Configuration const&      _configuration;
  Activation&               _activation;
  PeerFrames&               _frames;
  FramePacing&              _pacing;
  Encoder&                  _encoder;
  Scaler&                   _scaler;
  Queue                     _queue;
  std::vector<std::uint8_t> _scratch;
  LegacyFormat              _format       { };
};
}
