#pragma once
#include <sdl-rdp/picture/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/pixel-band.hpp>
#include <sdl-rdp/video/tap.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sdl_rdp::video::detail::scaler {
using sdl_rdp::picture::DesktopLayout;
using sdl_rdp::utilities::Pinned;

enum class RowOrder{ TopDown, BottomUp };
class Scaler : private Pinned {
public:
       Scaler(PeerFrames const& source, DesktopLayout const& layout) noexcept;
  auto Areas() const                                                              -> std::vector<sdlrdp_rect>;
  auto Target() const noexcept                                                    -> sdlrdp_rect;
  auto Copy(sdlrdp_rect area, std::span<std::uint8_t> buffer, RowOrder order)     -> PixelBand;
  auto Place(sdlrdp_rect area, std::span<std::uint8_t> buffer, std::size_t pitch) -> PixelBand;

private:
  auto Area(sdlrdp_rect damage) const                                                                -> sdlrdp_rect;
  auto Scaled() const                                                                                -> bool;
  auto Fill(sdlrdp_rect area, std::span<std::uint8_t> buffer, std::size_t pitch, RowOrder order)     -> PixelBand;
  auto Resample(sdlrdp_rect area, std::span<std::uint8_t> buffer, std::size_t pitch, RowOrder order) -> void;
  auto Columns(sdlrdp_rect area)                                                                     -> void;
  PeerFrames const&    _frames;
  DesktopLayout const& _desktop;
  std::vector<Tap>     _columns;
  int                  _column_x     { };
  int                  _column_width { };
  std::uint32_t        _column_source{ };
};
}

namespace sdl_rdp::video {
using detail::scaler::RowOrder;
using detail::scaler::Scaler;
}
