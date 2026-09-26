#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <string_view>

namespace sample::detail::events {
// printf's `%.*s` takes a view's length as an int precision.
auto Printed(std::string_view text)                                              -> int;
auto PrintAudioFormat(SDL_AudioDeviceID device)                                  -> void;
auto PrintEvent(SDL_Event const& event, SDL_Window& window, std::uint32_t frame) -> void;
}

namespace sample {
using detail::events::PrintAudioFormat;
using detail::events::PrintEvent;
using detail::events::Printed;
}
