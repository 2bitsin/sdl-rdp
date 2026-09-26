#pragma once
#include <sdl-rdp/configuration/forward.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/utilities/rect.hpp>
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/frame/forward.hpp>
#include <sdl-rdp/video/scaler.hpp>

#include <freerdp/update.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sdl_rdp::video::detail::legacy_frame {
using sdl_rdp::configuration::Configuration;
using sdl_rdp::link::Activation;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Rect;
using sdl_rdp::video::frame::FramePacing;

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
    Rect                   area { };
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
  auto SelectEncoder()                                                                                     -> bool;
  auto AppendPlanar(Packet& packet, std::size_t& wire_size, Rect area, std::span<std::byte const> payload) -> void;
  auto AppendBand(PixelBand band)                                                                          -> bool;
  auto Finish()                                                                                            -> bool;
  auto Marker(std::uint16_t action)                                                                        -> bool;
  auto Planar(Rect area)                                                                                   -> bool;
  auto Bands(Rect area)                                                                                    -> bool;
  auto Describe(Packet& packet) const                                                                      -> void;
  auto Write(Packet& packet)                                                                               -> bool;
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

namespace sdl_rdp::video {
using detail::legacy_frame::LegacyFrame;
}
