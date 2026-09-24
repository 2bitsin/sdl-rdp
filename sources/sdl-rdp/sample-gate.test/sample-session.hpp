#pragma once
#include <sdl-rdp/sample-gate.test/input-client.hpp>
#include <sdl-rdp/sample-gate.test/position-observer.hpp>
#include <sdl-rdp/sample-gate.test/sample-checks.hpp>

#include <sdl-rdp/headless-client.test/clipboard-client.hpp>
#include <memory>

namespace SampleGate {
class SampleSession : public SampleChecks {
protected:
  auto GivenDesktopProcess(std::vector<std::string> const& arguments)       -> void;
  auto GivenDriveProcess(std::vector<std::string> const& arguments, fs::path const& share) -> void;
  auto ConnectExposed(Client& client)                                       -> void;
  auto GivenInputSession(bool advanced = false)                             -> void;
  auto SessionClient()                                                      -> Client&;
  auto GivenPositionSession()                                               -> void;
  auto Position()                                                           -> PositionObserver&;
  auto GivenFullscreen()                                                    -> void;
  auto GivenAspect()                                                        -> void;
  auto ThenExplicitGeometry(Client& client, unsigned height = 200)          -> void;
  auto GivenAudioProcess(std::vector<std::string> const& arguments)         -> void;
  auto ThenIniConnects(std::vector<std::string> const& args, unsigned port) -> void;
  auto ClipboardSession()                                                   -> Headless::ClipboardClient&;
  auto GivenClipboard(std::string const& text)                              -> void;
  auto GivenIniProcess(std::vector<std::string> const& args, unsigned port) -> void;
  unsigned audio_port = 0;

private:
  std::unique_ptr<Client>                    session;
  std::unique_ptr<Headless::ClipboardClient> clipboard;
  std::unique_ptr<InputClient>               channels;
  std::unique_ptr<PositionObserver>          position;
};
}
