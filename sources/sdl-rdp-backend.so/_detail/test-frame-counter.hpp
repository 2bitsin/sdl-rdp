#pragma once
#include "client.hpp"
#include "test-picture-update-hook.hpp"

namespace BackendGate {
class FrameCounter {
public:
  explicit FrameCounter(Headless::Client& client);
  auto     Frames() const     -> unsigned;
  auto     BitmapPdus() const -> unsigned;

private:
  auto Count(Headless::PictureUpdate const& update) -> void;
  unsigned                    frames      = 0;
  unsigned                    bitmap_pdus = 0;
  Headless::PictureUpdateHook hook;
};
}
