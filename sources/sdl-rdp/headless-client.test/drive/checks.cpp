#include <sdl-rdp/headless-client.test/drive/checks.hpp>
#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/drive/file.hpp>

#include <chrono>
#include <tuple>

namespace sdl_rdp::headless_client_test::drive::detail::checks {
using sdl_rdp::drive::File;
using sdl_rdp::drive::PeerDisconnected;
using sdl_rdp::headless_client_test::client::Clock;

auto DriveChecks::ThenReadRanges(File& file, std::string const& source) -> void {
  std::string result(source.size(), '\0');
  auto        start  = Clock::now();
  auto const  count  = ReadAt(file, 0, result);
  EXPECT_EQ(count, result.size());
  auto seconds = std::chrono::duration<double>(Clock::now() - start).count();
  RecordProperty("read_3MiB_MBps", 3.145728 / seconds);
  EXPECT_EQ(result, source);
  ThenPartialReads(file, source, result);
}
auto DriveChecks::ThenAbortedRead(std::future<std::size_t>& read) -> void {
  ASSERT_EQ(read.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  EXPECT_THROW(std::ignore = read.get(), PeerDisconnected);
}
}
