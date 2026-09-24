#include "_detail/test-drive-checks.hpp"

namespace DriveGate {
auto DriveChecks::ThenReadRanges(sdlrdp_file* file, std::string const& source) -> void {
  std::string result(source.size(), '\0');
  auto        start  = Headless::Clock::now();
  ASSERT_EQ(sdlrdp_drive_read(handle.get(), file, 0, result.data(), result.size()), result.size())
      << sdlrdp_last_error();
  auto seconds = std::chrono::duration<double>(Headless::Clock::now() - start).count();
  RecordProperty("read_3MiB_MBps", 3.145728 / seconds);
  EXPECT_EQ(result, source);
  ThenPartialReads(file, source, result);
}
}
