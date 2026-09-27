#pragma once
#include <sdl-rdp/freerdp-facade/failure-sink.hpp>
#include <sdl-rdp/freerdp-facade/input-sink.hpp>

#include <bitset>
#include <cstdint>
#include <optional>

namespace sdl_rdp::freerdp_facade::detail::advanced_input_channel_events {
// An ainput mouse event: a relative one carries a motion delta, an absolute one a desktop position.
struct AdvancedPointerEvent {
  std::bitset<PointerButtonCount> buttons;
  bool                            down            { };
  bool                            moved           { };
  bool                            relative        { };
  bool                            relative_capable{ };
  std::int32_t                    x               { };
  std::int32_t                    y               { };
  std::optional<WheelTurn>        wheel;
};
// What an ainput channel's mouse slot reports; false refuses the event.
class AdvancedInputChannelEvents : public FailureSink {
public:
  virtual auto AdvancedPointer(AdvancedPointerEvent const& event) -> bool = 0;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::advanced_input_channel_events::AdvancedInputChannelEvents;
using detail::advanced_input_channel_events::AdvancedPointerEvent;
}
