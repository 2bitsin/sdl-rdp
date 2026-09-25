#include <sdl-rdp/sample-gate.test/client/steps.hpp>

#include <sdl-rdp/sample-gate.test/client/input.hpp>

#include <gtest/gtest.h>
#include <sdl-rdp/headless-client.test/client/display.hpp>
#include <sdl-rdp/headless-client.test/drive/share-drive.hpp>

namespace sdl_rdp::sample_gate_test::client::detail::steps {
using sdl_rdp::headless_client_test::client::DisplayClient;
using sdl_rdp::headless_client_test::drive::ShareDrive;

auto ChangeMonitor(Client& client) -> void {
  ASSERT_TRUE(client.Until([&] { return DisplayClient::Of(client, &DisplayClient::Ready); }));
  ASSERT_TRUE(DisplayClient::Of(client, [](auto const& display) { return display.Layout(1920, 1080, 500); }));
}

auto ThenAdvanced(Client& client) -> void {
  ASSERT_TRUE(client.Until([&] {
    return InputClient::Advanced().load() && InputClient::Touch().load()
           && InputClient::Touch().load()->GetVersion(InputClient::Touch().load()) == RDPINPUT_PROTOCOL_V10;
  }));
}

auto ConnectDrive(Client& client, std::filesystem::path const& share) -> void {
  ShareDrive(client, share.c_str());
  ASSERT_TRUE(client.Connect());
}
}
