#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <cstddef>
#include <cstdint>

namespace SampleGate {
struct PositionObserver {
public:
           PositionObserver(PositionObserver const&)               = delete;
           PositionObserver(PositionObserver&&)                    = delete;
  explicit PositionObserver(Headless::Client& client);
           ~PositionObserver();
  auto     operator=(PositionObserver const&) -> PositionObserver& = delete;
  auto     operator=(PositionObserver&&)      -> PositionObserver& = delete;
  auto     Count() const                      -> std::size_t;
  auto     X() const                          -> std::uint32_t;
  auto     Y() const                          -> std::uint32_t;

private:
  auto Receive(POINTER_POSITION_UPDATE const& position) -> void;
  inline static thread_local PositionObserver* active = nullptr;
  std::size_t                                  count  = 0;
  std::uint32_t                                x      = 0;
  std::uint32_t                                y      = 0;
};
}
