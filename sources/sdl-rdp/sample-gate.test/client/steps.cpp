#include <sdl-rdp/sample-gate.test/client/steps.hpp>

#include <sdl-rdp/sample-gate.test/client/input.hpp>

#include <gtest/gtest.h>
#include <sdl-rdp/headless-client.test/client/display.hpp>
#include <sdl-rdp/headless-client.test/drive/share-drive.hpp>

namespace SampleGate {
auto ChangeMonitor(Headless::Client& client) -> void {
  ASSERT_TRUE(client.Until([&] { return Headless::DisplayClient::Of(client, &Headless::DisplayClient::Ready); }));
  ASSERT_TRUE(Headless::DisplayClient::Of(client, [](auto const& display) { return display.Layout(1920, 1080, 500); }));
}

auto ThenAdvanced(Headless::Client& client) -> void {
  ASSERT_TRUE(client.Until([&] {
    return InputClient::Advanced().load() && InputClient::Touch().load()
           && InputClient::Touch().load()->GetVersion(InputClient::Touch().load()) == RDPINPUT_PROTOCOL_V10;
  }));
}

auto ConnectDrive(Headless::Client& client, std::filesystem::path const& share) -> void {
  Headless::ShareDrive(client, share.c_str());
  ASSERT_TRUE(client.Connect());
}
}
