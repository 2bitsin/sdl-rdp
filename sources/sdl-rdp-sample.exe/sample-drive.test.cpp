#include "_detail/sample-fixture.hpp"

#include <cstddef>
#include <sdl-rdp-backend.so/_detail/drive-observer.hpp>
#include <sdl-rdp-backend.so/_detail/frame-observer.hpp>
#include <sdl-rdp-backend.so/_detail/share-drive.hpp>

namespace SampleGate {
namespace {
class DriveSample : public SampleGate::Sample {
protected:
  void ThenMissingCat(Client& client) {
    ASSERT_TRUE(ReadInput(client, "cat failed: ")) << process->Transcript();
    EXPECT_NE(line.find("Drive 'missing.bin' failed: STATUS_NO_SUCH_FILE (0xc000000f)"), std::string::npos) << line;
  }
};
namespace {
void ThenCatFailure(Process const& process) {
  auto failure = process.Transcript().find("cat failed:");
  EXPECT_EQ(process.Transcript().find("cat failed:", failure + 1), std::string::npos);
  EXPECT_EQ(process.Transcript().find("cat bytes="), std::string::npos);
}
}
namespace {
void CreateHugeFile(fs::path const& share) {
  auto path = share / "huge.bin";
  {
    std::ofstream const file(path);
  }
  fs::resize_file(path, static_cast<std::ptrdiff_t>(400 * 1024) * 1024);
}
}
namespace {
void ThenCatNotRepeated(Process const& process, Headless::DriveObserver const& observer) {
  EXPECT_EQ(observer.Observed().requests, 0u);
  ThenCatFailure(process);
  SDL_Log("trace DRIVE second client connected, frame received, cat not repeated, sample exited 0");
}
}
TEST_F(DriveSample, DriveDisconnectDuringCat) {
  oxbox::platform::ScratchArea const share{ "sample-disconnect", "sdl-rdp" };
  CreateHugeFile(share.Path());
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--cat", "share/huge.bin" });
  GivenProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  auto port = AnnouncedPort(line);
  DisconnectReading(port, share.Path());
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(Read("cat failed: ")) << process->Transcript();
  SDL_Log("trace DRIVE disconnected after read request: %s", line.c_str());
  Client second(port, true, 640, 480);
  Headless::ShareDrive(second, share.Path().c_str());
  ASSERT_TRUE(freerdp_connect(second.Instance().get())) << ConnectLogs();
  Headless::DriveObserver observer(second);
  ASSERT_TRUE(second.Until([&] { return !observer.Observed().replies.empty() && Pattern(second, false); }));
  Escape(second);
  if (::testing::Test::HasFatalFailure()) return;
  while (process->Line(line, Clock::now() + 1s)) {
  }
  ThenCatNotRepeated(*process, observer);
}

TEST_F(DriveSample, DriveMissingCatKeepsServing) {
  oxbox::platform::ScratchArea const share     { "sample-missing", "sdl-rdp" };
  auto                               arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--cat", "share/missing.bin" });
  GivenDriveProcess(arguments, share.Path());
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = SessionClient();
  ThenMissingCat(client);
  if (::testing::Test::HasFatalFailure()) return;
  // Observe a new frame after the failure, rather than inspecting an old framebuffer.
  Headless::FrameObserver observer(client);
  ASSERT_TRUE(client.Until([&] { return !observer.Frames().empty() && Pattern(client, false); }));
  Escape(client);
  if (::testing::Test::HasFatalFailure()) return;
  while (process->Line(line, Clock::now() + 1s)) {
  }
  auto failure = process->Transcript().find("cat failed:");
  ASSERT_NE(failure, std::string::npos);
  ThenCatFailure(*process);
}
}
}
