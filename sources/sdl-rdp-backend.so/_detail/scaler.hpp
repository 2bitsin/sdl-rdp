#pragma once
#include "pinned.hpp"
#include "pixel-band.hpp"
#include "tap.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace Backend {
class DesktopLayout;
class PeerFrames;
enum class RowOrder{ TopDown, BottomUp };
class Scaler : private Pinned {
public:
                           Scaler(PeerFrames const& source, DesktopLayout const& layout) noexcept;
  std::vector<sdlrdp_rect> Areas() const;
  sdlrdp_rect              Target() const                                                noexcept;
  PixelBand                Copy(sdlrdp_rect area, std::span<BYTE> buffer, RowOrder order);
  PixelBand                Place(sdlrdp_rect area, std::span<BYTE> buffer, std::size_t pitch);

private:
  sdlrdp_rect Area(sdlrdp_rect damage) const;
  bool        Scaled() const;
  PixelBand   Fill(sdlrdp_rect area, std::span<BYTE> buffer, std::size_t pitch, RowOrder order);
  void        Resample(sdlrdp_rect area, std::span<BYTE> buffer, std::size_t pitch, RowOrder order);
  void        Columns(sdlrdp_rect area);
  PeerFrames const&    _frames;
  DesktopLayout const& _desktop;
  std::vector<Tap>     _columns;
  int                  _column_x     { };
  int                  _column_width { };
  unsigned             _column_source{ };
};
}
