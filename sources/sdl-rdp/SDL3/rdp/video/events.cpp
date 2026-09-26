#include "events.hpp"
#include "clipboard.hpp"
#include "window.hpp"
#include <sdl-rdp/SDL3/rdp/audio/bootstrap.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/boundary.hpp>
#include <sdl-rdp/configuration/refresh.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/session/backend.hpp>
#include <sdl-rdp/settings/settings.hpp>
// The RDP protocol sends Windows scan codes; SDL's Windows table maps them.
#include "src/events/scancodes_windows.h"
#include <oxbox/utilities/codepoint.hpp>
#include <oxbox/utilities/utf-encode.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
namespace sdl3::rdp::video::detail::events {
using sdl3::rdp::audio::AudioRate;
using sdl3::rdp::sdl::Boundary;
using sdl3::rdp::storage::UpdateDrives;
using sdl_rdp::configuration::MillihertzPerHz;
using sdl_rdp::link::AudioChanged;
using sdl_rdp::link::ClipboardChanged;
using sdl_rdp::link::CodecChanged;
using sdl_rdp::link::Connected;
using sdl_rdp::link::Disconnected;
using sdl_rdp::link::DriveChanged;
using sdl_rdp::link::Event;
using sdl_rdp::link::Key;
using sdl_rdp::link::MouseButton;
using sdl_rdp::link::MouseMove;
using sdl_rdp::link::MouseRelative;
using sdl_rdp::link::MouseWheel;
using sdl_rdp::link::RefreshChanged;
using sdl_rdp::link::ScreenChanged;
using sdl_rdp::link::TextInput;
using sdl_rdp::link::Touch;
using sdl_rdp::link::TouchPhase;
using sdl_rdp::settings::NameOf;
using sdl_rdp::utilities::DeadlineWithin;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Unreachable;
namespace {
constexpr SDL_TouchID TouchDevice       { 1 };
constexpr auto        ExtendedScanCodes = std::size(windows_scancode_table) / 2;
// SDL reserves finger id 0; the backend counts fingers from 0.
auto Finger(std::uint32_t id) -> SDL_FingerID {
  return SDL_FingerID{ id } + 1;
}
auto IsCodePoint(std::uint32_t value) -> bool {
  return oxbox::utilities::CodepointTriage(value) != oxbox::utilities::CodepointType::OUT_OF_RANGE;
}
auto CopyRefresh(SDL_DisplayMode& target, SDL_DisplayMode const& source) -> void {
  target.refresh_rate             = source.refresh_rate;
  target.refresh_rate_numerator   = source.refresh_rate_numerator;
  target.refresh_rate_denominator = source.refresh_rate_denominator;
}
auto FullscreenMode(SDL_Window& window, SDL_DisplayMode const& mode) -> void {
  bool const accepted = SDL_SetWindowFullscreenMode(&window, &mode);
  Ensures(accepted, "SDL accepts a desktop-sized fullscreen mode");
}
auto ScreenMode(SDL_VideoData& data, int width, int height) -> void {
  auto&           display   = *SDL_GetVideoDisplay(data.Display());
  SDL_DisplayMode requested = BoundWindow(data).requested_fullscreen_mode;
  DesktopMode(data, width, height);
  SDL_ResetFullscreenDisplayModes(&display);
  if (!requested.w) return;
  CopyRefresh(requested, display.desktop_mode);
  FullscreenMode(BoundWindow(data), requested);
}
auto FollowsDesktop(SDL_VideoData const& data, int width, int height) -> bool {
  auto const& window  = BoundWindow(data);
  auto const  picture = std::pair{ width, height };
  return (window.flags & SDL_WINDOW_FULLSCREEN) && !window.requested_fullscreen_mode.w && data.Picture() != picture;
}
auto Resize(SDL_VideoData& data, std::uint32_t screen_width, std::uint32_t screen_height) -> void {
  if (!screen_width || !screen_height || screen_width > SDL_MAX_SINT32 || screen_height > SDL_MAX_SINT32) return;
  auto const  width   = static_cast<int>(screen_width);
  auto const  height  = static_cast<int>(screen_height);
  auto const& desktop = SDL_GetVideoDisplay(data.Display())->desktop_mode;
  if (desktop.w != width || desktop.h != height) ScreenMode(data, width, height);
  if (!FollowsDesktop(data, width, height)) return;
  ResizePicture(data, width, height);
  SDL_SendWindowEvent(&BoundWindow(data), SDL_EVENT_WINDOW_RESIZED, width, height);
}
auto SameRefresh(std::uint32_t left_numerator, std::uint32_t left_denominator, std::uint32_t right_numerator,
                 std::uint32_t right_denominator) -> bool {
  auto const left  = std::uint64_t{ left_numerator } * right_denominator;
  auto const right = std::uint64_t{ right_numerator } * left_denominator;
  return left_denominator != 0 && right_denominator != 0 && left == right;
}
auto ApplyRefresh(SDL_VideoData& data, std::uint32_t millihertz) -> void {
  Expects(millihertz > 0, "refresh event specifies positive millihertz");
  auto& display = *SDL_GetVideoDisplay(data.Display());
  if (SameRefresh(Narrowed<std::uint32_t>(display.current_mode->refresh_rate_numerator),
                  Narrowed<std::uint32_t>(display.current_mode->refresh_rate_denominator), millihertz, MillihertzPerHz))
    return;
  auto& mode = data.RefreshMode(*display.current_mode);
  mode.refresh_rate             = static_cast<float>(millihertz) / static_cast<float>(MillihertzPerHz);
  mode.refresh_rate_numerator   = static_cast<int>(millihertz);
  mode.refresh_rate_denominator = static_cast<int>(MillihertzPerHz);
  SDL_SetCurrentDisplayMode(&display, &mode);
}
auto RestoreRefresh(SDL_VideoData& data) -> void {
  auto const& desktop = SDL_GetVideoDisplay(data.Display())->desktop_mode;
  if (desktop.refresh_rate_denominator <= 0) return;
  ApplyRefresh(data, Narrowed<std::uint32_t>(Narrowed<std::uint64_t>(desktop.refresh_rate_numerator) * MillihertzPerHz
                                             / Narrowed<std::uint64_t>(desktop.refresh_rate_denominator)));
}
auto PublishClient(SDL_Window& window, Connected const& client) -> void {
  auto const        properties = SDL_GetWindowProperties(&window);
  std::string const codec      { NameOf(client.codec) };
  auto const        strings    = std::to_array<std::pair<char const*, char const*>>(
      { { SDL_PROP_WINDOW_RDP_CLIENT_NAME_STRING, client.client_name.c_str() },
        { SDL_PROP_WINDOW_RDP_CODEC_STRING      , codec.c_str()              },
        { SDL_PROP_WINDOW_RDP_USER_STRING       , client.user.c_str()        },
        { SDL_PROP_WINDOW_RDP_DOMAIN_STRING     , client.domain.c_str()      } });
  for (auto const& [key, value] : strings) SDL_SetStringProperty(properties, key, value);
  SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_RDP_AUTHENTICATED_BOOLEAN, client.authenticated);
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_RDP_KEYBOARD_LAYOUT_NUMBER, client.keyboard_layout);
}
auto SendText(SDL_Window& window, std::uint32_t codepoint) -> void {
  auto const code_point = IsCodePoint(codepoint);
  Expects(code_point, "text is a Unicode code point");
  if (!SDL_TextInputActive(&window)) return;
  auto const [length, bytes] = oxbox::utilities::UtfEncode<char>(codepoint);
  SDL_SendKeyboardText(std::string(bytes.data(), length).c_str());
}
auto Scancode(Key const& key) -> SDL_Scancode {
  auto const index = static_cast<std::uint8_t>(key.scancode) | (key.extended ? ExtendedScanCodes : 0);
  return std::span(windows_scancode_table)[index];
}
auto FingerEvent(TouchPhase phase) -> SDL_EventType {
  switch (phase) {
  case TouchPhase::Down:   return SDL_EVENT_FINGER_DOWN;
  case TouchPhase::Up:     return SDL_EVENT_FINGER_UP;
  case TouchPhase::Cancel: return SDL_EVENT_FINGER_CANCELED;
  default:                 Unreachable(phase);
  }
}
// The events a bound window receives, one overload per kind; the ones that also move the device take it.
auto Handle(SDL_Window& window, TextInput const& text) -> void {
  if (!text.down) return;
  SDL_SendKeyboardUnicodeKey(0, text.codepoint);
  SendText(window, text.codepoint);
}
auto Handle(SDL_Window& window, Key const& key) -> void {
  auto const scancode = Scancode(key);
  SDL_SendKeyboardKey(0, SDL_DEFAULT_KEYBOARD_ID, static_cast<int>(key.scancode), scancode, key.down);
  if (!key.down || !SDL_TextInputActive(&window)) return;
  auto const code = SDL_GetKeyFromScancode(scancode, SDL_GetModState(), false);
  if (code >= SDLK_SPACE && code != SDLK_DELETE && IsCodePoint(code)) SendText(window, code);
}
auto Handle(SDL_Window& window, Touch const& touch) -> void {
  if (touch.phase == TouchPhase::Move)
    SDL_SendTouchMotion(0, TouchDevice, Finger(touch.id), &window, touch.x, touch.y, touch.pressure);
  else
    SDL_SendTouch(0, TouchDevice, Finger(touch.id), &window, FingerEvent(touch.phase), touch.x, touch.y,
                  touch.pressure);
}
auto Handle(SDL_Window& window, MouseButton const& button) -> void {
  constexpr auto buttons = std::to_array<std::uint8_t>(
      { SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT, SDL_BUTTON_X1, SDL_BUTTON_X2 });
  if (button.button > 0 && button.button <= buttons.size())
    SDL_SendMouseButton(0, &window, SDL_DEFAULT_MOUSE_ID, buttons.at(button.button - 1), button.down);
}
auto Handle(SDL_Window& window, MouseMove const& move) -> void {
  SDL_SendMouseMotion(0, &window, SDL_DEFAULT_MOUSE_ID, false, static_cast<float>(move.x), static_cast<float>(move.y));
}
auto Handle(SDL_Window& window, MouseRelative const& motion) -> void {
  SDL_SendMouseMotion(0, &window, SDL_DEFAULT_MOUSE_ID, true, static_cast<float>(motion.dx),
                      static_cast<float>(motion.dy));
}
auto Handle(SDL_Window& window, MouseWheel const& wheel) -> void {
  SDL_SendMouseWheel(0, &window, SDL_DEFAULT_MOUSE_ID, wheel.dx, wheel.dy, SDL_MOUSEWHEEL_NORMAL);
}
auto Handle(SDL_Window& window, CodecChanged const& change) -> void {
  SDL_SetStringProperty(SDL_GetWindowProperties(&window), SDL_PROP_WINDOW_RDP_CODEC_STRING,
                        std::string{ NameOf(change.codec) }.c_str());
}
auto Handle(SDL_Window& window, SDL_VideoData& data, Connected const& client) -> void {
  ApplyRefresh(data, client.refresh_millihertz);
  ScreenMode(data, static_cast<int>(client.screen_width), static_cast<int>(client.screen_height));
  Resize(data, client.screen_width, client.screen_height);
  PublishClient(window, client);
  SDL_SendWindowEvent(&window, SDL_EVENT_WINDOW_EXPOSED, 0, 0);
  data.AttachTouch(TouchDevice);
  SDL_SetKeyboardFocus(&window);
  SDL_SetMouseFocus(&window);
}
auto Handle(SDL_Window& window, SDL_VideoData& data, [[maybe_unused]] Disconnected const& left) -> void {
  RestoreRefresh(data);
  SDL_SendWindowEvent(&window, SDL_EVENT_WINDOW_OCCLUDED, 0, 0);
  SDL_SetKeyboardFocus(nullptr);
  SDL_SetMouseFocus(nullptr);
  data.DetachTouch();
}
// The events the device receives, some whether or not a window is bound.
auto Handle(SDL_VideoData& data, RefreshChanged const& refresh) -> void {
  ApplyRefresh(data, refresh.millihertz);
}
auto Handle(SDL_VideoData& data, ScreenChanged const& screen) -> void {
  Resize(data, screen.width, screen.height);
}
auto Handle(SDL_VideoData& data, [[maybe_unused]] ClipboardChanged const& change) -> void {
  ClipboardUpdate(data);
}
auto Handle(SDL_VideoData& data, [[maybe_unused]] DriveChanged const& change) -> void {
  UpdateDrives(data.Driver(), SDL_GetDisplayProperties(data.Display()));
}
auto Handle(AudioChanged const& audio) -> void {
  AudioRate(audio.rate);
}
// The display mode follows the client only while a window is bound, though its handlers touch the device alone.
template <typename EventTy> inline constexpr bool FollowsBinding = false;
template <> inline constexpr bool FollowsBinding<RefreshChanged> = true;
template <> inline constexpr bool FollowsBinding<ScreenChanged> = true;
// An event a window receives is dropped while none is bound; the overload set says which.
template <typename EventTy>
auto WhenBound(SDL_VideoData& data, EventTy const& event) -> void {
  constexpr bool device = requires { Handle(data, event); };
  if constexpr (requires { Handle(event); }) {
    Handle(event);
  } else if constexpr (device && !FollowsBinding<EventTy>) {
    Handle(data, event);
  } else if (auto const window = data.Window()) {
    if constexpr (device)
      Handle(data, event);
    else if constexpr (requires { Handle(window->get(), event); })
      Handle(window->get(), event);
    else
      Handle(window->get(), data, event);
  }
}
auto Dispatch(SDL_VideoData& data, Event const& event) -> void {
  std::visit([&](auto const& payload) { WhenBound(data, payload); }, event);
}
// SDL event callbacks borrow their device and optional wakeup window.
auto PumpEvents(SDL_VideoDevice* device) -> void {
  Expects(device != nullptr, "event pump has a device");
  auto& data = *device->internal;
  Boundary([&] {
    for (auto const& event : data.PollEvents()) Dispatch(data, event);
  });
}
// SDL specifies nanoseconds and a borrowed device in its wait callback; a negative timeout waits indefinitely.
auto WaitEvent(SDL_VideoDevice* device, std::int64_t timeout) -> int {
  Expects(device != nullptr, "event wait has a device");
  return Boundary([&] {
    auto const deadline = DeadlineWithin(std::chrono::nanoseconds{ timeout });
    return device->internal->Driver().Backend().Events().Wait(deadline) ? 1 : 0;
  });
}
// SDL's wake callback receives a borrowed device and window.
auto Wakeup(SDL_VideoDevice* device, [[maybe_unused]] SDL_Window* unused_window) -> void {
  Expects(device != nullptr, "event wakeup has a device");
  Boundary([&] { device->internal->Driver().Backend().Events().Wakeup(); });
}
}
auto InitEvents(SDL_VideoDevice& device) -> void {
  device.PumpEvents       = PumpEvents;
  device.WaitEventTimeout = WaitEvent;
  device.SendWakeupEvent  = Wakeup;
}
}
