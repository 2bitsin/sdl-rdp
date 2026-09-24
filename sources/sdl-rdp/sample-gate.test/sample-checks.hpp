#pragma once
#include <sdl-rdp/sample-gate.test/sample-process.hpp>

#include <sdl-rdp/headless-client.test/clipboard-client.hpp>

namespace SampleGate {
class SampleChecks : public SampleProcess {
protected:
  auto        WhenSurrogateText(rdpInput* input)                                         -> void;
  auto        WhenUnicodeText(rdpInput* input)                                           -> void;
  auto        ThenAbsoluteMouse(rdpInput* input)                                         -> void;
  auto        WhenShiftedText(rdpInput* input)                                           -> void;
  auto        WhenScancodeText(rdpInput* input)                                          -> void;
  auto        WhenNonAsciiKey(rdpInput* input)                                           -> void;
  auto        ThenUnicodeKeyEvents()                                                     -> void;
  auto        WhenUnicodeKeys(rdpInput* input)                                           -> void;
  auto        ThenDriveOutput(fs::path const& share, std::string const& original)        -> void;
  static auto WhenIniEnvironmentConflicts()                                              -> void;
  auto        GivenIniHints(fs::path const& file)                                        -> void;
  static auto ThenReloadedAspect()                                                       -> void;
  auto        ThenReloadedIni(fs::path const& file)                                      -> void;
  auto        DisconnectReading(unsigned port, fs::path const& share)                    -> void;
  auto        ThenClipboardCleared(Client& client, Headless::ClipboardClient& clipboard) -> void;
};
}
