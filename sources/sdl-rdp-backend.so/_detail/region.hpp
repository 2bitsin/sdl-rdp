#pragma once
#include "sdl-rdp-backend.h"

#include <vector>

namespace Backend {
class Region {
public:
  void                            Add(sdlrdp_rect area);
  bool                            empty() const;
  void                            clear();
  std::vector<sdlrdp_rect> const& Rects() const;
  void                            Swap(Region& other);

private:
  std::vector<sdlrdp_rect> rects;
};
}
