#pragma once
#include <sdl-rdp/freerdp-facade/failure-sink.hpp>

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace sdl_rdp::freerdp_facade::detail::input_sink {
struct KeyEvent {
  std::uint8_t code    { };
  bool         extended{ };
  bool         down    { };
};
struct UnicodeEvent {
  char16_t code{ };
  bool     down{ };
};
// Indexes PointerEvent::buttons.
enum class                   PointerButton      : std::uint8_t { Left, Middle, Right, Back, Forward };
inline constexpr std::size_t PointerButtonCount = 5;
// Wheel rotation in notches, positive away from the user and to the right.
struct WheelTurn {
  float horizontal{ };
  float vertical  { };
};
struct PointerEvent {
  std::bitset<PointerButtonCount> buttons;
  bool                            down   { };
  bool                            moved  { };
  std::uint16_t                   x      { };
  std::uint16_t                   y      { };
  std::optional<WheelTurn>        wheel;
};
// The connection's keyboard and mouse input, decoded from the slow-path and fast-path flags once, in the facade.
class InputSink : public FailureSink {
public:
  virtual auto Key(KeyEvent event)                -> void = 0;
  virtual auto Unicode(UnicodeEvent event)        -> void = 0;
  virtual auto Pointer(PointerEvent const& event) -> bool = 0;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::input_sink::InputSink;
using detail::input_sink::KeyEvent;
using detail::input_sink::PointerButton;
using detail::input_sink::PointerButtonCount;
using detail::input_sink::PointerEvent;
using detail::input_sink::UnicodeEvent;
using detail::input_sink::WheelTurn;
}
