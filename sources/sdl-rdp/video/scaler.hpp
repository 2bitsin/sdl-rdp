#pragma once
#include <sdl-rdp/picture/forward.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/tap.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sdl_rdp::video::detail::scaler {
using sdl_rdp::picture::DesktopLayout;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Rect;

enum class RowOrder{ TopDown, BottomUp };
struct PixelBand {
  Rect                    area;
  std::span<std::uint8_t> pixels;
};
class Scaler : private Pinned {
public:
       Scaler(PeerFrames const& source, DesktopLayout const& layout) noexcept;
  auto Areas() const                                                       -> std::vector<Rect>;
  auto Target() const noexcept                                             -> Rect;
  auto Copy(Rect area, std::span<std::uint8_t> buffer, RowOrder order)     -> PixelBand;
  auto Place(Rect area, std::span<std::uint8_t> buffer, std::size_t pitch) -> PixelBand;

private:
  auto Area(Rect damage) const                                                                -> Rect;
  auto Scaled() const                                                                         -> bool;
  auto Fill(Rect area, std::span<std::uint8_t> buffer, std::size_t pitch, RowOrder order)     -> PixelBand;
  auto Resample(Rect area, std::span<std::uint8_t> buffer, std::size_t pitch, RowOrder order) -> void;
  auto Columns(Rect area)                                                                     -> void;
  PeerFrames const&    _frames;
  DesktopLayout const& _desktop;
  std::vector<Tap>     _columns;
  int                  _column_x     { };
  int                  _column_width { };
  std::uint32_t        _column_source{ };
};
}

namespace sdl_rdp::video {
using detail::scaler::PixelBand;
using detail::scaler::RowOrder;
using detail::scaler::Scaler;
}
