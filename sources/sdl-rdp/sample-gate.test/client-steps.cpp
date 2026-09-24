#include <sdl-rdp/sample-gate.test/client-steps.hpp>

#include <sdl-rdp/sample-gate.test/input-client.hpp>

#include <gtest/gtest.h>
#include <sdl-rdp/headless-client.test/display-client.hpp>
#include <sdl-rdp/headless-client.test/share-drive.hpp>

namespace SampleGate {
auto ChangeMonitor(Headless::Client& client) -> void {
  ASSERT_TRUE(client.Until([] { return Headless::DisplayClient::Ready(); }));
  auto monitor = Headless::DisplayClient::Monitor(1920, 1080, 500);
  ASSERT_EQ(Headless::DisplayClient::Channel()->SendMonitorLayout(Headless::DisplayClient::Channel(), 1, &monitor),
            CHANNEL_RC_OK);
}

auto ThenAdvanced(Headless::Client& client) -> void {
  ASSERT_TRUE(client.Until([&] {
    return InputClient::Advanced().load() && InputClient::Touch().load()
           && InputClient::Touch().load()->GetVersion(InputClient::Touch().load()) == RDPINPUT_PROTOCOL_V10;
  }));
}

auto ConnectDrive(Headless::Client& client, std::filesystem::path const& share) -> void {
  Headless::ShareDrive(client, share.c_str());
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
}
}
