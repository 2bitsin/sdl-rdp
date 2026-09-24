#pragma once
#include "client.hpp"
#include "picture-update-hook.hpp"
#include <cstddef>

namespace BackendGate {
class FrameCounter {
public:
  explicit FrameCounter(Headless::Client& client);
  auto     Frames() const     -> std::size_t;
  auto     BitmapPdus() const -> std::size_t;

private:
  auto Count(Headless::PictureUpdate const& update) -> void;
  std::size_t                 frames      = 0;
  std::size_t                 bitmap_pdus = 0;
  Headless::PictureUpdateHook hook;
};
}
