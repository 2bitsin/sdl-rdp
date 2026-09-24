#include <sdl-rdp/sample-gate.test/frame-pattern.hpp>
#include <sdl-rdp/sample-gate.test/process.hpp>
#include <sdl-rdp/sample-gate.test/sample-launch.hpp>
#include <sdl-rdp/sample-gate.test/sample.hpp>

#include <SDL3/SDL.h>
#include <sdl-rdp/headless-client.test/drive-observer.hpp>
#include <sdl-rdp/headless-client.test/frame-observer.hpp>
#include <sdl-rdp/headless-client.test/share-drive.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

namespace SampleGate {
namespace {
class DriveSample : public SampleGate::Sample {
protected:
  auto ThenMissingCat(Client& client) -> void {
    ASSERT_TRUE(ReadInput(client, "cat failed: ")) << process->Transcript();
    EXPECT_NE(line.find("Drive 'missing.bin' failed: STATUS_NO_SUCH_FILE (0xc000000f)"), std::string::npos) << line;
  }
};
namespace {
auto ThenCatFailure(Process const& process) -> void {
  auto failure = process.Transcript().find("cat failed:");
  EXPECT_EQ(process.Transcript().find("cat failed:", failure + 1), std::string::npos);
  EXPECT_EQ(process.Transcript().find("cat bytes="), std::string::npos);
}
}
namespace {
auto CreateHugeFile(fs::path const& share) -> void {
  auto path = share / "huge.bin";
  {
    std::ofstream const file(path);
  }
  fs::resize_file(path, static_cast<std::ptrdiff_t>(400 * 1024) * 1024);
}
}
namespace {
auto ThenCatNotRepeated(Process const& process, Headless::DriveObserver const& observer) -> void {
  EXPECT_EQ(observer.Observed().requests, 0u);
  ThenCatFailure(process);
  SDL_Log("trace DRIVE second client connected, frame received, cat not repeated, sample exited 0");
}
}
TEST_F(DriveSample, DriveDisconnectDuringCat) {
  oxbox::platform::ScratchArea const share{ "sample-disconnect", "sdl-rdp" };
  CreateHugeFile(share.Path());
  ASSERT_NO_FATAL_FAILURE(GivenProcess({ }, { "--cat", "share/huge.bin" }));
  auto port = AnnouncedPort(line);
  ASSERT_NO_FATAL_FAILURE(DisconnectReading(port, share.Path()));
  ASSERT_TRUE(Read("cat failed: ")) << process->Transcript();
  SDL_Log("trace DRIVE disconnected after read request: %s", line.c_str());
  Client second(port, true, 640, 480);
  Headless::ShareDrive(second, share.Path().c_str());
  ASSERT_NO_FATAL_FAILURE(Connect(second));
  Headless::DriveObserver observer(second);
  ASSERT_TRUE(second.Until([&] { return !observer.Observed().replies.empty() && Pattern(second, false); }));
  ASSERT_NO_FATAL_FAILURE(Escape(second));
  while (process->Line(line, Clock::now() + 1s)) {
  }
  ThenCatNotRepeated(*process, observer);
}

TEST_F(DriveSample, DriveMissingCatKeepsServing) {
  oxbox::platform::ScratchArea const share{ "sample-missing", "sdl-rdp" };
  ASSERT_NO_FATAL_FAILURE(GivenDriveProcess({ "--cat", "share/missing.bin" }, share.Path()));
  auto& client = SessionClient();
  ASSERT_NO_FATAL_FAILURE(ThenMissingCat(client));
  // Observe a new frame after the failure, rather than inspecting an old framebuffer.
  Headless::FrameObserver observer(client);
  ASSERT_TRUE(client.Until([&] { return !observer.Frames().empty() && Pattern(client, false); }));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
  while (process->Line(line, Clock::now() + 1s)) {
  }
  auto failure = process->Transcript().find("cat failed:");
  ASSERT_NE(failure, std::string::npos);
  ThenCatFailure(*process);
}
}
}
