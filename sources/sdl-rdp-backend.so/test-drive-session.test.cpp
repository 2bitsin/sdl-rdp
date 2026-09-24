#include "_detail/test-drive-session.hpp"

#include "_detail/share-drive.hpp"
#include "_detail/test-config.hpp"

#include <algorithm>
#include <array>
#include <oxbox/platform/file-writer.hpp>
#include <ranges>
#include <span>
#include <vector>

namespace DriveGate {
auto DriveSession::ThenPartialReads(sdlrdp_file* file, std::string const& source, std::string& result) -> void {
  for (size_t const offset : { 13u, 1048577u, 3145697u }) {
    result.resize(65536);
    auto count = sdlrdp_drive_read(handle.get(), file, offset, result.data(), result.size());
    ASSERT_GE(count, 0) << sdlrdp_last_error();
    EXPECT_EQ(result.substr(0, count), source.substr(offset, result.size()));
  }
}
auto DriveSession::SetUp() -> void {
  auto path   = scratch.Path().string();
  auto config = Headless::LoopbackConfig(path);
  config.log_user = &logs;
  config.log      = Headless::Logs::Collect;
  sdlrdp_handle* opened = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &opened), 0) << sdlrdp_last_error();
  handle.reset(opened);
  Connect();
}
auto DriveSession::Connect(char const* name, bool second) -> void {
  client = std::make_unique<Headless::Client>(sdlrdp_port(handle.get()), false);
  auto path = scratch.Path().string();
  Headless::ShareDrive(*client, path.c_str(), name);
  if (second) Headless::ShareDrive(*client, path.c_str(), "second");
  ASSERT_TRUE(freerdp_connect(client->Instance().get())) << logs.Text(true);
  ASSERT_TRUE(client->Until([&] {
    sdlrdp_drive value{ };
    if (sdlrdp_drive_list(handle.get(), &value, 1) != 1) return false;
    EXPECT_STREQ(value.name, name);
    drive = value.id;
    return true;
  }));
  pump = std::jthread([&](std::stop_token const& stop) {
    while (!stop.stop_requested() && client->Pump()) {
    }
  });
}
auto DriveSession::GivenHeldFile() -> void {
  Write("file", "data");
  held_file = Open("file");
  ASSERT_NE(held_file, nullptr);
  HoldRequests();
}
auto DriveSession::HoldRequests() -> void {
  pump.request_stop();
  pump.join();
  observer                  = std::make_unique<Headless::DriveObserver>(*client);
  observer->Observed().hold = true;
}
auto DriveSession::ThenVideoMatches() -> void {
  std::vector<UINT32> pixels(320uz * 200uz, 0x00446688);
  sdlrdp_rect const   damage{ 0, 0, 320, 200 };
  ASSERT_EQ(sdlrdp_present(handle.get(), pixels.data(), 1280, 320, 200, &damage, 1), 0);
  ASSERT_TRUE(client->Until([&] { return client->Matches(pixels); }));
}
auto DriveSession::Disconnect() -> void {
  pump.request_stop();
  if (pump.joinable()) pump.join();
  if (client) freerdp_disconnect(client->Instance().get());
  client.reset();
}
auto DriveSession::TearDown() -> void {
  observer.reset();
  Disconnect();
}
auto DriveSession::Logged(sdlrdp_log_level level, std::string_view text) -> unsigned {
  return logs.Count(level, text);
}
auto DriveSession::Pattern(size_t size, unsigned seed) -> std::string {
  std::string bytes(size, '\0');
  std::ranges::transform(std::views::iota(0uz, size), bytes.begin(),
                         [=](size_t i) { return char((i * 31 + i / 251 + seed) & 255); });
  return bytes;
}
auto DriveSession::Write(std::string const& name, std::string const& bytes) -> void {
  oxbox::platform::WriteBinaryFile(scratch.Path() / name, std::as_bytes(std::span(bytes)));
}
auto DriveSession::Open(char const* name, unsigned flags) -> sdlrdp_file* {
  sdlrdp_file* file = nullptr;
  EXPECT_EQ(sdlrdp_drive_open(handle.get(), drive, name, flags, &file), 0) << sdlrdp_last_error();
  return file;
}
auto DriveSession::ThenRemovedDrive() -> void {
  std::array<sdlrdp_event, 32> events { };
  auto                         count  = sdlrdp_poll(handle.get(), events.data(), 32);
  EXPECT_TRUE(std::ranges::any_of(std::span(events.data(), count), [&](auto const& event) {
    return event.type == SDLRDP_DRIVE && !event.drive.added && event.drive.id == drive;
  }));
}
auto DriveSession::ThenDriveFailure(sdlrdp_file* file, unsigned warnings) -> void {
  sdlrdp_drive value{ };
  EXPECT_EQ(sdlrdp_drive_list(handle.get(), &value, 1), 0);
  EXPECT_EQ(sdlrdp_drive_close(handle.get(), file), -1);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "") - warnings, 1u);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "Drive channel ended: Truncated drive response."), 1u);
}
auto DriveSession::GivenDirectoryEntries() -> std::set<std::string> {
  std::set<std::string> expected;
  for (unsigned i = 0; i < 200; ++i) {
    auto name = std::to_string(i);
    expected.insert(name);
    Write("many/" + name, "data");
  }
  return expected;
}
}
