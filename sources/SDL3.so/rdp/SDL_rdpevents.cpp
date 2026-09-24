#include "SDL_rdpaudio.hpp"
#include "boundary.hpp"
#include "SDL_rdpconstants.hpp"
#include "SDL_rdprefresh.hpp"
#include "SDL_rdpvideo.hpp"
// The RDP protocol sends Windows scan codes; SDL's Windows table maps them.
#include "src/events/scancodes_windows.h"
#include <oxbox/utilities/codepoint.hpp>
#include <oxbox/utilities/utf-encode.hpp>
#include <chrono>
#include <cstdint>
namespace rdp {
namespace {
constexpr SDL_TouchID TouchDevice       { 1 };
constexpr auto        ExtendedScanCodes = std::size(windows_scancode_table) / 2;
// SDL reserves finger id 0; the backend counts fingers from 0.
auto Finger(unsigned id)       -> SDL_FingerID { return SDL_FingerID{id} + 1; }
auto IsCodePoint(Uint32 value) -> bool {
  return oxbox::utilities::CodepointTriage(value) != oxbox::utilities::CodepointType::OUT_OF_RANGE;
}
auto CopyRefresh(SDL_DisplayMode& target, SDL_DisplayMode const& source) -> void {
  target.refresh_rate             = source.refresh_rate;
  target.refresh_rate_numerator   = source.refresh_rate_numerator;
  target.refresh_rate_denominator = source.refresh_rate_denominator;
}
auto FullscreenMode(SDL_Window& window, SDL_DisplayMode const& mode) -> void {
  bool const accepted = SDL_SetWindowFullscreenMode(&window, &mode);
  utilities::Ensures(accepted, "SDL accepts a desktop-sized fullscreen mode");
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
  auto const  picture = std::pair{width, height};
  return (window.flags & SDL_WINDOW_FULLSCREEN) && !window.requested_fullscreen_mode.w && data.Picture() != picture;
}
auto Resize(SDL_VideoData& data, unsigned screen_width, unsigned screen_height) -> void {
  if (!screen_width || !screen_height || screen_width > SDL_MAX_SINT32 || screen_height > SDL_MAX_SINT32) return;
  auto const  width   = static_cast<int>(screen_width);
  auto const  height  = static_cast<int>(screen_height);
  auto const& desktop = SDL_GetVideoDisplay(data.Display())->desktop_mode;
  if (desktop.w != width || desktop.h != height) ScreenMode(data, width, height);
  if (FollowsDesktop(data, width, height) && ResizePicture(data, width, height))
    SDL_SendWindowEvent(&BoundWindow(data), SDL_EVENT_WINDOW_RESIZED, width, height);
}
auto ApplyRefresh(SDL_VideoData& data, unsigned millihertz) -> void {
  utilities::Expects(millihertz > 0, "refresh event specifies positive millihertz");
  auto& display = *SDL_GetVideoDisplay(data.Display());
  if (SameRefresh(static_cast<unsigned>(display.current_mode->refresh_rate_numerator),
                  static_cast<unsigned>(display.current_mode->refresh_rate_denominator),
                  millihertz, MillihertzPerHertz)) return;
  auto& mode = data.RefreshMode(*display.current_mode);
  mode.refresh_rate             = static_cast<float>(millihertz) / static_cast<float>(MillihertzPerHertz);
  mode.refresh_rate_numerator   = static_cast<int>(millihertz);
  mode.refresh_rate_denominator = static_cast<int>(MillihertzPerHertz);
  SDL_SetCurrentDisplayMode(&display, &mode);
}
auto RestoreRefresh(SDL_VideoData& data) -> void {
  auto const& desktop = SDL_GetVideoDisplay(data.Display())->desktop_mode;
  if (desktop.refresh_rate_denominator <= 0) return;
  ApplyRefresh(data, static_cast<unsigned>(static_cast<Uint64>(desktop.refresh_rate_numerator) * MillihertzPerHertz /
                                           static_cast<Uint64>(desktop.refresh_rate_denominator)));
}
auto PublishClient(SDL_Window& window, decltype(sdlrdp_event::connected) const& client) -> void {
  auto const properties = SDL_GetWindowProperties(&window);
  auto const codec      = CodecName(client.codec);
  auto const strings    = std::to_array<std::pair<char const*, char const*>>({
      { SDL_PROP_WINDOW_RDP_CLIENT_NAME_STRING, client.client_name },
      { SDL_PROP_WINDOW_RDP_CODEC_STRING      , codec.c_str()      },
      { SDL_PROP_WINDOW_RDP_USER_STRING       , client.user        },
      { SDL_PROP_WINDOW_RDP_DOMAIN_STRING     , client.domain      }});
  for (auto const& [key, value] : strings) SDL_SetStringProperty(properties, key, value);
  SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_RDP_AUTHENTICATED_BOOLEAN, client.authenticated != 0);
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_RDP_KEYBOARD_LAYOUT_NUMBER, client.keyboard_layout);
}
auto Connected(SDL_VideoData& data, sdlrdp_event const& event) -> void {
  utilities::Expects(event.type == SDLRDP_CONNECTED, "connection event has connection data");
  auto const& client = event.connected;
  auto&       window = BoundWindow(data);
  ApplyRefresh(data, client.refresh_millihertz);
  ScreenMode(data, static_cast<int>(client.screen_width), static_cast<int>(client.screen_height));
  Resize(data, client.screen_width, client.screen_height);
  PublishClient(window, client);
  SDL_SendWindowEvent(&window, SDL_EVENT_WINDOW_EXPOSED, 0, 0);
  data.AttachTouch(TouchDevice);
  SDL_SetKeyboardFocus(&window);
  SDL_SetMouseFocus(&window);
}
auto Disconnected(SDL_VideoData& data) -> void {
  RestoreRefresh(data);
  SDL_SendWindowEvent(&BoundWindow(data), SDL_EVENT_WINDOW_OCCLUDED, 0, 0);
  SDL_SetKeyboardFocus(nullptr);
  SDL_SetMouseFocus(nullptr);
  data.DetachTouch();
}
auto SendText(SDL_Window& window, Uint32 codepoint) -> void {
  utilities::Expects(IsCodePoint(codepoint), "text is a Unicode code point");
  if (!SDL_TextInputActive(&window)) return;
  auto const [length, bytes] = oxbox::utilities::UtfEncode<char>(codepoint);
  SDL_SendKeyboardText(std::string(bytes.data(), length).c_str());
}
auto Text(SDL_Window& window, sdlrdp_event const& event) -> void {
  utilities::Expects(event.type == SDLRDP_TEXT, "text event carries text");
  if (!event.text.down) return;
  SDL_SendKeyboardUnicodeKey(0, event.text.codepoint);
  SendText(window, event.text.codepoint);
}
auto Scancode(sdlrdp_event const& event) -> SDL_Scancode {
  auto const index = static_cast<std::uint8_t>(event.key.scancode) | (event.key.extended ? ExtendedScanCodes : 0);
  return std::span(windows_scancode_table)[index];
}
auto Key(SDL_Window& window, sdlrdp_event const& event) -> void {
  utilities::Expects(event.type == SDLRDP_KEY, "key event carries a key");
  auto const scancode = Scancode(event);
  SDL_SendKeyboardKey(0, SDL_DEFAULT_KEYBOARD_ID, static_cast<int>(event.key.scancode), scancode, event.key.down != 0);
  if (!event.key.down || !SDL_TextInputActive(&window)) return;
  auto const key = SDL_GetKeyFromScancode(scancode, SDL_GetModState(), false);
  if (key >= SDLK_SPACE && key != SDLK_DELETE && IsCodePoint(key)) SendText(window, key);
}
auto FingerEvent(sdlrdp_touch_phase phase) -> SDL_EventType {
  switch (phase) {
  case SDLRDP_TOUCH_DOWN:   return SDL_EVENT_FINGER_DOWN;
  case SDLRDP_TOUCH_UP:     return SDL_EVENT_FINGER_UP;
  case SDLRDP_TOUCH_CANCEL: return SDL_EVENT_FINGER_CANCELED;
  default: utilities::Unreachable(phase);
  }
}
auto Touch(SDL_Window& window, sdlrdp_event const& event) -> void {
  utilities::Expects(event.type == SDLRDP_TOUCH, "touch event has touch data");
  auto const& touch = event.touch;
  if (touch.phase == SDLRDP_TOUCH_MOVE)
    SDL_SendTouchMotion(0, TouchDevice, Finger(touch.id), &window, touch.x, touch.y, touch.pressure);
  else SDL_SendTouch(0, TouchDevice, Finger(touch.id), &window, FingerEvent(touch.phase), touch.x, touch.y,
                     touch.pressure);
}
auto MouseButton(SDL_Window& window, sdlrdp_event const& event) -> void {
  utilities::Expects(event.type == SDLRDP_MOUSE_BUTTON, "button event has button data");
  constexpr auto buttons = std::to_array<Uint8>({SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT, SDL_BUTTON_X1,
                                                 SDL_BUTTON_X2});
  auto const     button  = event.mouse_button.button;
  if (button > 0 && button <= buttons.size())
    SDL_SendMouseButton(0, &window, SDL_DEFAULT_MOUSE_ID, buttons.at(button - 1), event.mouse_button.down != 0);
}
auto MouseMove(SDL_Window& window, sdlrdp_event const& event) -> void {
  SDL_SendMouseMotion(0, &window, SDL_DEFAULT_MOUSE_ID, false, static_cast<float>(event.mouse_move.x),
                      static_cast<float>(event.mouse_move.y));
}
auto MouseRelative(SDL_Window& window, sdlrdp_event const& event) -> void {
  SDL_SendMouseMotion(0, &window, SDL_DEFAULT_MOUSE_ID, true, static_cast<float>(event.mouse_relative.dx),
                      static_cast<float>(event.mouse_relative.dy));
}
auto MouseWheel(SDL_Window& window, sdlrdp_event const& event) -> void {
  SDL_SendMouseWheel(0, &window, SDL_DEFAULT_MOUSE_ID, event.mouse_wheel.dx, event.mouse_wheel.dy,
                     SDL_MOUSEWHEEL_NORMAL);
}
auto CodecChanged(SDL_Window& window, sdlrdp_event const& event) -> void {
  SDL_SetStringProperty(SDL_GetWindowProperties(&window), SDL_PROP_WINDOW_RDP_CODEC_STRING,
                        CodecName(event.codec_changed.codec).c_str());
}
auto ClientLeft(SDL_VideoData& data, [[maybe_unused]] sdlrdp_event const& event) -> void { Disconnected(data); }
auto RefreshChanged(SDL_VideoData& data, sdlrdp_event const& event)              -> void {
  ApplyRefresh(data, event.refresh.millihertz);
}
auto ScreenChanged(SDL_VideoData& data, sdlrdp_event const& event) -> void {
  Resize(data, event.screen.width, event.screen.height);
}
auto PictureResized([[maybe_unused]] SDL_VideoData& data, [[maybe_unused]] sdlrdp_event const& event) -> void { }
auto DrivesChanged(SDL_VideoData& data, [[maybe_unused]] sdlrdp_event const& event)                   -> void {
  UpdateDrives(data.Backend(), SDL_GetDisplayProperties(data.Display()));
}
auto ClipboardChanged(SDL_VideoData& data, [[maybe_unused]] sdlrdp_event const& event) -> void {
  ClipboardUpdate(data);
}
auto AudioChanged([[maybe_unused]] SDL_VideoData& data, sdlrdp_event const& event) -> void {
  AudioRate(event.audio.freq);
}
using Handler = auto (*)(SDL_VideoData&, sdlrdp_event const&) -> void;
template<Handler _Handle>
auto WhenBound(SDL_VideoData& data, sdlrdp_event const& event) -> void {
  if (data.Window()) _Handle(data, event);
}
template<auto (*_Handle)(SDL_Window&, sdlrdp_event const&) -> void>
auto ToWindow(SDL_VideoData& data, sdlrdp_event const& event) -> void {
  if (auto const window = data.Window()) _Handle(*window, event);
}
constexpr auto Handlers = [] {
  std::array<Handler, SDLRDP_DRIVE + 1> table{ };
  table[SDLRDP_CONNECTED]      = WhenBound<Connected>;
  table[SDLRDP_DISCONNECTED]   = WhenBound<ClientLeft>;
  table[SDLRDP_RESIZE]         = PictureResized;
  table[SDLRDP_KEY]            = ToWindow<Key>;
  table[SDLRDP_MOUSE_MOVE]     = ToWindow<MouseMove>;
  table[SDLRDP_MOUSE_BUTTON]   = ToWindow<MouseButton>;
  table[SDLRDP_MOUSE_WHEEL]    = ToWindow<MouseWheel>;
  table[SDLRDP_CODEC_CHANGED]  = ToWindow<CodecChanged>;
  table[SDLRDP_SCREEN]         = WhenBound<ScreenChanged>;
  table[SDLRDP_REFRESH]        = WhenBound<RefreshChanged>;
  table[SDLRDP_CLIPBOARD]      = ClipboardChanged;
  table[SDLRDP_TEXT]           = ToWindow<Text>;
  table[SDLRDP_MOUSE_RELATIVE] = ToWindow<MouseRelative>;
  table[SDLRDP_TOUCH]          = ToWindow<Touch>;
  table[SDLRDP_AUDIO]          = AudioChanged;
  table[SDLRDP_DRIVE]          = DrivesChanged;
  return table;
}();
static_assert(std::ranges::none_of(Handlers, [](Handler handler) { return handler == nullptr; }),
              "every backend event type has a handler");
auto Dispatch(SDL_VideoData& data, sdlrdp_event const& event) -> void {
  utilities::Expects(std::cmp_less(std::to_underlying(event.type), Handlers.size()), "backend event type is known");
  Handlers.at(static_cast<std::size_t>(event.type))(data, event);
}
// SDL event callbacks borrow their device and optional wakeup window.
auto PumpEvents(SDL_VideoDevice* device) -> void {
  utilities::Expects(device != nullptr, "event pump has a device");
  auto& data = *device->internal;
  Boundary([&] { data.Backend().Poll([&](sdlrdp_event const& event) { Dispatch(data, event); }); });
}
// SDL specifies nanoseconds and a borrowed device in its wait callback.
auto WaitEvent(SDL_VideoDevice* device, Sint64 timeout) -> int {
  utilities::Expects(device != nullptr, "event wait has a device");
  auto const milliseconds = timeout < 0 ? -1
      : std::chrono::ceil<std::chrono::milliseconds>(std::chrono::nanoseconds{timeout}).count();
  return device->internal->Backend().Call<Operation::WAIT>(
      static_cast<int>(std::min<std::int64_t>(milliseconds, SDL_MAX_SINT32)));
}
// SDL's wake callback receives a borrowed device and window.
auto Wakeup(SDL_VideoDevice* device, [[maybe_unused]] SDL_Window* unused_window) -> void {
  utilities::Expects(device != nullptr, "event wakeup has a device");
  device->internal->Backend().Call<Operation::WAKEUP>();
}
}
auto InitEvents(SDL_VideoDevice& device) -> void {
  device.PumpEvents       = PumpEvents;
  device.WaitEventTimeout = WaitEvent;
  device.SendWakeupEvent  = Wakeup;
}
}
