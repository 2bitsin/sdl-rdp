#pragma once
#include <functional>

namespace Backend {
class Encoder;
class FramePacing;
class PeerFrames;
class Scaler;
struct FrameSources {
  std::reference_wrapper<PeerFrames>  frames;
  std::reference_wrapper<FramePacing> pacing;
  std::reference_wrapper<Encoder>     encoder;
  std::reference_wrapper<Scaler>      scaler;
};
}
