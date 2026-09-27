#pragma once
#include <sdl-rdp/freerdp-facade/failure-sink.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace sdl_rdp::freerdp_facade::detail::touch_channel_events {
enum class ContactPhase : std::uint8_t { Down, Move, Up, Cancel };
// A contact's position in desktop pixels; pressure in 0 to 1024 when the client sends one (MS-RDPEI 2.2.3.3.1.1).
struct TouchContact {
  std::uint32_t                id      { };
  std::int32_t                 x       { };
  std::int32_t                 y       { };
  ContactPhase                 phase   { };
  std::optional<std::uint32_t> pressure;
};
// What an rdpei channel's touch slot reports: every contact of every frame of one touch event, in order.
class TouchChannelEvents : public FailureSink {
public:
  virtual auto Touch(std::span<TouchContact const> contacts) -> void = 0;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::touch_channel_events::ContactPhase;
using detail::touch_channel_events::TouchChannelEvents;
using detail::touch_channel_events::TouchContact;
}
