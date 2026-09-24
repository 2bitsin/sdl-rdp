#pragma once
#include "support.test/sample-session.hpp"

namespace SampleGate {
class SampleDesktopSteps : public SampleSession {
protected:
  auto        GivenAdvancedSession()                                                      -> void;
  static auto PressFullscreenKey(Client& client)                                          -> void;
  auto        WhenUnicodeClipboardOffered(Client& client, std::vector<BYTE> const& bytes) -> void;
  auto        WhenClipboardEmptied(Client& client)                                        -> void;
  auto        WhenAsciiClipboardOffered(Client& client)                                   -> void;
  auto        ThenSizeEvents(std::string const& dimensions)                               -> void;
  auto        ThenDesktopMode(Client& client, unsigned w, unsigned h)                     -> void;
  auto        ThenWaitingPort(unsigned port)                                              -> void;
  auto        WhenCodecKeyChanges(Client const& client)                                   -> void;
  auto        GivenSwitchableCodec(Client const& client)                                  -> void;
  auto        ThenTakeoverEvent(char const* expected)                                     -> void;
  auto        WhenSmallerDesktop(Client& first)                                           -> void;
  auto        WhenWholeSampleReconnects(Client const& client, unsigned port)              -> void;
  auto        GivenWholeSample()                                                          -> void;
};
}
