#pragma once
#include <sdl-rdp/configuration/codec.hpp>

#include <cstdint>
#include <string>
#include <variant>

namespace sdl_rdp::link::detail::event {
using sdl_rdp::configuration::Codec;

struct Connected {
  std::uint32_t width             { };
  std::uint32_t height            { };
  std::uint32_t bpp               { };
  std::string   client_name;
  Codec         codec             { };
  std::uint32_t screen_width      { };
  std::uint32_t screen_height     { };
  std::uint32_t refresh_millihertz{ };
  std::uint32_t keyboard_layout   { };
  std::string   user;
  std::string   domain;
  bool          authenticated     { };
};
struct Disconnected{ };
struct Key {
  std::uint32_t scancode{ };
  bool          extended{ };
  bool          down    { };
};
struct MouseMove {
  int x{ };
  int y{ };
};
struct MouseButton {
  std::uint32_t button{ };
  bool          down  { };
};
struct MouseWheel {
  float dx{ };
  float dy{ };
};
struct CodecChanged {
  Codec codec{ };
};
struct ScreenChanged {
  std::uint32_t width { };
  std::uint32_t height{ };
};
struct RefreshChanged {
  std::uint32_t millihertz{ };
};
struct ClipboardChanged{ };
struct TextInput {
  char32_t codepoint{ };
  bool     down     { };
};
struct MouseRelative {
  int dx{ };
  int dy{ };
};
enum class TouchPhase : std::uint8_t { Down, Move, Up, Cancel };
struct Touch {
  std::uint32_t id      { };
  float         x       { };
  float         y       { };
  float         pressure{ };
  TouchPhase    phase   { };
};
struct AudioChanged {
  std::uint32_t rate     { };
  bool          connected{ };
};
struct DriveChanged {
  bool          added{ };
  std::uint32_t id   { };
  std::string   name;
};
using Event = std::variant<Connected, Disconnected, Key, MouseMove, MouseButton, MouseWheel, CodecChanged,
                           ScreenChanged, RefreshChanged, ClipboardChanged, TextInput, MouseRelative, Touch,
                           AudioChanged, DriveChanged>;
}

namespace sdl_rdp::link {
using detail::event::AudioChanged;
using detail::event::ClipboardChanged;
using detail::event::CodecChanged;
using detail::event::Connected;
using detail::event::Disconnected;
using detail::event::DriveChanged;
using detail::event::Event;
using detail::event::Key;
using detail::event::MouseButton;
using detail::event::MouseMove;
using detail::event::MouseRelative;
using detail::event::MouseWheel;
using detail::event::RefreshChanged;
using detail::event::ScreenChanged;
using detail::event::TextInput;
using detail::event::Touch;
using detail::event::TouchPhase;
}
