#pragma once
#include <sdl-rdp-backend.so/_detail/client.hpp>

namespace SampleGate {
struct PositionObserver {
public:
           PositionObserver(PositionObserver const&)                 = delete;
           PositionObserver(PositionObserver&&)                      = delete;
  explicit PositionObserver(Headless::Client& client);
           ~PositionObserver();
  auto     operator = (PositionObserver const&) -> PositionObserver& = delete;
  auto     operator = (PositionObserver&&)      -> PositionObserver& = delete;
  auto     Count() const                        -> unsigned;
  auto     X() const                            -> unsigned;
  auto     Y() const                            -> unsigned;

private:
  static auto Receive(rdpContext* /*unused*/, POINTER_POSITION_UPDATE const* position) -> BOOL;
  inline static thread_local PositionObserver* active = nullptr;
  unsigned                                     count  = 0;
  unsigned                                     x      = 0;
  unsigned                                     y      = 0;
};
}
