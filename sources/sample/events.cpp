#include <sample/events.hpp>

#include <sample/auth.hpp>
#include <sample/check.hpp>
#include <sample/clipboard.hpp>
#include <sample/input.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace sample::detail::events {
namespace {
constexpr std::array<std::pair<std::uint32_t, std::string_view>, 24> EventLabels{ {
    { SDL_EVENT_KEYBOARD_ADDED              , "KEYBOARD_ADDED"               },
    { SDL_EVENT_MOUSE_ADDED                 , "MOUSE_ADDED"                  },
    { SDL_EVENT_WINDOW_MOVED                , "MOVED"                        },
    { SDL_EVENT_WINDOW_SAFE_AREA_CHANGED    , "SAFE_AREA_CHANGED"            },
    { SDL_EVENT_QUIT                        , "QUIT"                         },
    { SDL_EVENT_WINDOW_SHOWN                , "SHOWN"                        },
    { SDL_EVENT_WINDOW_HIDDEN               , "HIDDEN"                       },
    { SDL_EVENT_WINDOW_OCCLUDED             , "OCCLUDED"                     },
    { SDL_EVENT_WINDOW_EXPOSED              , "EXPOSED"                      },
    { SDL_EVENT_WINDOW_RESIZED              , "RESIZED"                      },
    { SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED   , "PIXEL_SIZE_CHANGED"           },
    { SDL_EVENT_WINDOW_FOCUS_GAINED         , "FOCUS_GAINED"                 },
    { SDL_EVENT_WINDOW_FOCUS_LOST           , "FOCUS_LOST"                   },
    { SDL_EVENT_WINDOW_MOUSE_ENTER          , "MOUSE_ENTER"                  },
    { SDL_EVENT_WINDOW_MOUSE_LEAVE          , "MOUSE_LEAVE"                  },
    { SDL_EVENT_DISPLAY_ADDED               , "DISPLAY_ADDED"                },
    { SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED, "DISPLAY_CURRENT_MODE_CHANGED" },
    { SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED, "DISPLAY_DESKTOP_MODE_CHANGED" },
    { SDL_EVENT_KEY_DOWN                    , "KEY_DOWN"                     },
    { SDL_EVENT_KEY_UP                      , "KEY_UP"                       },
    { SDL_EVENT_MOUSE_MOTION                , "MOUSE_MOTION"                 },
    { SDL_EVENT_MOUSE_BUTTON_DOWN           , "MOUSE_BUTTON_DOWN"            },
    { SDL_EVENT_MOUSE_BUTTON_UP             , "MOUSE_BUTTON_UP"              },
    { SDL_EVENT_MOUSE_WHEEL                 , "MOUSE_WHEEL"                  },
} };
auto EventName(std::uint32_t type) -> std::string_view {
  auto const* label = std::ranges::find(EventLabels, type, &std::pair<std::uint32_t, std::string_view>::first);
  return label == EventLabels.end() ? "OTHER" : label->second;
}

auto PrintGeometry(SDL_Event const& event, SDL_Window& window) -> void {
  if (event.type == SDL_EVENT_WINDOW_EXPOSED || event.type == SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED
      || event.type == SDL_EVENT_WINDOW_RESIZED) {
    auto const* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    int         w    = 0;
    int         h    = 0;
    Check(SDL_GetWindowSize(&window, &w, &h));
    SDL_Log("event GEOMETRY window=%dx%d desktop=%dx%d", w, h, mode->w, mode->h);
  }
}

}
auto PrintAudioFormat(SDL_AudioDeviceID device) -> void {
  SDL_AudioSpec actual;
  Check(SDL_GetAudioDeviceFormat(device, &actual, nullptr));
  SDL_Log("audio device=%s freq=%d", SDL_GetAudioDeviceName(device), actual.freq);
}

namespace {
auto DisplayTiming() -> std::string {
  auto const* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
  Check(mode != nullptr);
  return std::format(" refresh={} numerator={} denominator={}", mode->refresh_rate, mode->refresh_rate_numerator,
                     mode->refresh_rate_denominator);
}

auto ClientProperties(SDL_Window& window) -> std::string {
  auto properties = SDL_GetWindowProperties(&window);
  return std::format(" keyboard_layout={} client_name={} codec={}",
                     SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_RDP_KEYBOARD_LAYOUT_NUMBER, 0),
                     SDL_GetStringProperty(properties, SDL_PROP_WINDOW_RDP_CLIENT_NAME_STRING, ""),
                     SDL_GetStringProperty(properties, SDL_PROP_WINDOW_RDP_CODEC_STRING, ""));
}

auto PointerDetails(SDL_Event const& event, std::uint32_t frame) -> std::string {
  switch (event.type) {
  case SDL_EVENT_MOUSE_MOTION:
    return std::format(" xrel={:g} yrel={:g} x={:.0f} y={:.0f} frame={}", event.motion.xrel, event.motion.yrel,
                       event.motion.x, event.motion.y, frame);
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
    return std::format(" button={} down={}", event.button.button, int{ event.button.down });
  case SDL_EVENT_MOUSE_WHEEL: return std::format(" x={} y={}", event.wheel.x, event.wheel.y);
  default:                    return { };
  }
}
auto WindowDetails(SDL_Event const& event, SDL_Window& window) -> std::string {
  if (event.type == SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED)
    return std::format(" width={} height={}", event.display.data1, event.display.data2);
  if (event.type == SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED) {
    auto const* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    return DisplayTiming() + std::format(" width={} height={}", mode->w, mode->h);
  }
  if (event.type == SDL_EVENT_WINDOW_EXPOSED) return ClientProperties(window);
  if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST)
    return std::format(" data1={} data2={}", event.window.data1, event.window.data2);
  return { };
}
auto EventDetails(SDL_Event const& event, SDL_Window& window, std::uint32_t frame) -> std::string {
  if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
    return std::format(" scancode={} key={} down={}", std::to_underlying(event.key.scancode), event.key.key,
                       int{ event.key.down });
  if (event.type == SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED) PrintAudioFormat(event.adevice.which);
  return PointerDetails(event, frame) + WindowDetails(event, window);
}
}
auto PrintEvent(SDL_Event const& event, SDL_Window& window, std::uint32_t frame) -> void {
  if (PrintClipboardEvent(event)) return;
  if (PrintInput(event, window)) return;
  auto line = std::format("event {} type={}", EventName(event.type), event.type);
  line += EventDetails(event, window, frame);
  SDL_Log("%s", line.c_str());
  PrintGeometry(event, window);
  if (event.type == SDL_EVENT_WINDOW_EXPOSED) PrintAuthentication(window);
}
}
