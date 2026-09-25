#pragma once
#include <sdl-rdp/sample-gate.test/client/input.hpp>
#include <sdl-rdp/sample-gate.test/client/position-observer.hpp>
#include <sdl-rdp/sample-gate.test/sample/checks.hpp>

#include <sdl-rdp/headless-client.test/client/clipboard.hpp>
#include <cstdint>
#include <memory>

namespace sdl_rdp::sample_gate_test::sample::detail::session {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::ClipboardClient;
using sdl_rdp::sample_gate_test::client::InputClient;
using sdl_rdp::sample_gate_test::client::PositionObserver;

class SampleSession : public SampleChecks {
protected:
  auto GivenDesktopProcess(Words const& environment, Words const& options)            -> void;
  auto GivenDriveProcess(Words const& options, std::filesystem::path const& share)    -> void;
  auto ConnectExposed(Client& client)                                                 -> void;
  auto GivenInputSession(bool advanced = false)                                       -> void;
  auto SessionClient()                                                                -> Client&;
  auto GivenPositionSession()                                                         -> void;
  auto Position()                                                                     -> PositionObserver&;
  auto GivenFullscreen()                                                              -> void;
  auto ThenExplicitGeometry(Client& client, std::uint32_t height = 200)               -> void;
  auto GivenAudioProcess(Words const& environment, Words const& options)              -> void;
  auto ThenSettingsConnect(std::vector<std::string> const& args, std::uint32_t port)  -> void;
  auto ClipboardSession()                                                             -> ClipboardClient&;
  auto GivenClipboard(std::string const& text)                                        -> void;
  auto GivenSettingsProcess(std::vector<std::string> const& args, std::uint32_t port) -> void;
  std::uint32_t audio_port = 0;

private:
  auto GivenSession(Words const& environment = { }, Words const& options = { }, std::uint32_t width = 640,
                    std::uint32_t height = 480) -> void;
  std::unique_ptr<Client>           session;
  std::unique_ptr<ClipboardClient>  clipboard;
  std::unique_ptr<InputClient>      channels;
  std::unique_ptr<PositionObserver> position;
};
}

namespace sdl_rdp::sample_gate_test::sample {
using detail::session::SampleSession;
}
