#pragma once
#include <sdl-rdp/sample-gate.test/sample/input.hpp>

#include <sdl-rdp/headless-client.test/client/clipboard.hpp>
#include <cstdint>

namespace sdl_rdp::sample_gate_test::sample::detail::checks {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::ClipboardClient;

class SampleChecks : public SampleInput {
protected:
  auto        WhenSurrogateText(rdpInput& input)                                               -> void;
  auto        WhenUnicodeText(rdpInput& input)                                                 -> void;
  auto        ThenAbsoluteMouse(rdpInput& input)                                               -> void;
  auto        WhenShiftedText(Client& client)                                                  -> void;
  auto        WhenScancodeText(Client& client)                                                 -> void;
  auto        WhenNonAsciiKey(rdpInput& input)                                                 -> void;
  auto        ThenUnicodeKeyEvents()                                                           -> void;
  auto        WhenUnicodeKeys(rdpInput& input)                                                 -> void;
  auto        ThenDriveOutput(std::filesystem::path const& share, std::string const& original) -> void;
  static auto WhenSettingsEnvironmentConflicts()                                               -> void;
  auto        GivenSettingsHints(std::filesystem::path const& file)                            -> void;
  static auto ThenReloadedAspect()                                                             -> void;
  auto        ThenReloadedSettings(std::filesystem::path const& file)                          -> void;
  auto        DisconnectReading(std::uint32_t port, std::filesystem::path const& share)        -> void;
  auto        ThenClipboardCleared(Client& client, ClipboardClient& clipboard)                 -> void;
};
}

namespace sdl_rdp::sample_gate_test::sample {
using detail::checks::SampleChecks;
}
