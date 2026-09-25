#pragma once
#include <sdl-rdp/sample-gate.test/sample/session.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace sdl_rdp::sample_gate_test::sample::detail::desktop_steps {
using sdl_rdp::headless_client_test::client::Client;

class SampleDesktopSteps : public SampleSession {
protected:
  auto        GivenAdvancedSession()                                                        -> void;
  static auto PressFullscreenKey(Client& client)                                            -> void;
  auto        WhenUnicodeClipboardOffered(Client& client, std::span<std::byte const> bytes) -> void;
  auto        WhenClipboardEmptied(Client& client)                                          -> void;
  auto        WhenAsciiClipboardOffered(Client& client)                                     -> void;
  auto        ThenSizeEvents(std::string const& dimensions)                                 -> void;
  auto        ThenDesktopMode(Client& client, std::uint32_t w, std::uint32_t h)             -> void;
  auto        ThenWaitingPort(std::uint32_t port)                                           -> void;
  auto        WhenCodecKeyChanges(Client& client)                                           -> void;
  auto        GivenSwitchableCodec(Client& client)                                          -> void;
  auto        ThenTakeoverEvent(std::string_view expected)                                  -> void;
  auto        WhenSmallerDesktop(Client& first)                                             -> void;
  auto        WhenWholeSampleReconnects(Client& client, std::uint32_t port)                 -> void;
};
}

namespace sdl_rdp::sample_gate_test::sample {
using detail::desktop_steps::SampleDesktopSteps;
}
