#include <sdl-rdp-backend.so/_detail/avc.hpp>
#include "_detail/sample-fixture.hpp"
#include <sdl-rdp-backend.so/_detail/headless-clipboard.hpp>
#include <sdl-rdp-backend.so/_detail/headless-audio.hpp>
#include <sdl-rdp-backend.so/_detail/headless-tls.hpp>
#include <sdl-rdp-backend.so/_detail/headless-drive.hpp>
#include <cmath>

namespace SampleGate {
TEST_F(Sample, DriveDisconnectDuringCat) {
  oxbox::platform::ScratchArea share{"sample-disconnect", "sdl-rdp"};
  auto path = share.Path() / "huge.bin";
  { std::ofstream file(path); }
  fs::resize_file(path, 400 * 1024 * 1024);
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--cat", "share/huge.bin"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  auto port = Number(std::string_view(line).substr(5));
  {
    Client client(port, true, 640, 480);
    Headless::ShareDrive(client, share.Path().c_str());
    ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
    Headless::DriveObserver observer(client);
    ASSERT_TRUE(client.Until([&] {
      return std::ranges::any_of(observer.io, [](auto packet) {
        packet.Skip(12);
        return packet.Get(4) == IRP_MJ_READ;
      });
    }));
    ASSERT_TRUE(freerdp_disconnect(client.instance.get()));
  }
  ASSERT_TRUE(Read("cat failed: ")) << process->transcript;
  SDL_Log("trace DRIVE disconnected after read request: %s", line.c_str());
  Client second(port, true, 640, 480);
  Headless::ShareDrive(second, share.Path().c_str());
  ASSERT_TRUE(freerdp_connect(second.instance.get())) << ConnectLogs();
  Headless::DriveObserver observer(second);
  ASSERT_TRUE(second.Until([&] { return !observer.replies.empty() && Pattern(second, false); }));
  ASSERT_NO_FATAL_FAILURE(Escape(second));
  while (process->Line(line, Clock::now() + 1s)) {}
  EXPECT_EQ(observer.requests, 0u);
  auto failure = process->transcript.find("cat failed:");
  EXPECT_EQ(process->transcript.find("cat failed:", failure + 1), std::string::npos);
  EXPECT_EQ(process->transcript.find("cat bytes="), std::string::npos);
  SDL_Log("trace DRIVE second client connected, frame received, cat not repeated, sample exited 0");
}

TEST_F(Sample, DriveMissingCatKeepsServing) {
  oxbox::platform::ScratchArea share{"sample-missing", "sdl-rdp"};
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--cat", "share/missing.bin"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  Headless::ShareDrive(client, share.Path().c_str());
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(ReadInput(client, "cat failed: ")) << process->transcript;
  EXPECT_NE(line.find("Drive 'missing.bin' failed: STATUS_NO_SUCH_FILE (0xc000000f)"), std::string::npos) << line;
  // Observe a new frame after the failure, rather than inspecting an old framebuffer.
  Headless::FrameObserver observer(client);
  ASSERT_TRUE(client.Until([&] { return !observer.ids.empty() && Pattern(client, false); }));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
  while (process->Line(line, Clock::now() + 1s)) {}
  auto failure = process->transcript.find("cat failed:");
  ASSERT_NE(failure, std::string::npos);
  EXPECT_EQ(process->transcript.find("cat failed:", failure + 1), std::string::npos);
  EXPECT_EQ(process->transcript.find("cat bytes="), std::string::npos);
}
}
