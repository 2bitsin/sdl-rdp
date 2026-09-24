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
void DriveSession::ThenPartialReads(sdlrdp_file* file, std::string const& source, std::string& result) {
  for (size_t const offset : { 13u, 1048577u, 3145697u }) {
    result.resize(65536);
    auto count = sdlrdp_drive_read(handle.get(), file, offset, result.data(), result.size());
    ASSERT_GE(count, 0) << sdlrdp_last_error();
    EXPECT_EQ(result.substr(0, count), source.substr(offset, result.size()));
  }
}
void DriveSession::SetUp() {
  auto path   = scratch.Path().string();
  auto config = Headless::LoopbackConfig(path);
  config.log_user = &logs;
  config.log      = Headless::Logs::Collect;
  sdlrdp_handle* opened = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &opened), 0) << sdlrdp_last_error();
  handle.reset(opened);
  Connect();
}
void DriveSession::Connect(char const* name, bool second) {
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
void DriveSession::GivenHeldFile() {
  Write("file", "data");
  held_file = Open("file");
  ASSERT_NE(held_file, nullptr);
  HoldRequests();
}
void DriveSession::HoldRequests() {
  pump.request_stop();
  pump.join();
  observer                  = std::make_unique<Headless::DriveObserver>(*client);
  observer->Observed().hold = true;
}
void DriveSession::ThenVideoMatches() {
  std::vector<UINT32> pixels(320uz * 200uz, 0x00446688);
  sdlrdp_rect const   damage{ 0, 0, 320, 200 };
  ASSERT_EQ(sdlrdp_present(handle.get(), pixels.data(), 1280, 320, 200, &damage, 1), 0);
  ASSERT_TRUE(client->Until([&] { return client->Matches(pixels); }));
}
void DriveSession::Disconnect() {
  pump.request_stop();
  if (pump.joinable()) pump.join();
  if (client) freerdp_disconnect(client->Instance().get());
  client.reset();
}
void DriveSession::TearDown() {
  observer.reset();
  Disconnect();
}
unsigned DriveSession::Logged(sdlrdp_log_level level, std::string_view text) {
  return logs.Count(level, text);
}
std::string DriveSession::Pattern(size_t size, unsigned seed) {
  std::string bytes(size, '\0');
  std::ranges::transform(std::views::iota(0uz, size), bytes.begin(),
                         [=](size_t i) { return char((i * 31 + i / 251 + seed) & 255); });
  return bytes;
}
void DriveSession::Write(std::string const& name, std::string const& bytes) {
  oxbox::platform::WriteBinaryFile(scratch.Path() / name, std::as_bytes(std::span(bytes)));
}
sdlrdp_file* DriveSession::Open(char const* name, unsigned flags) {
  sdlrdp_file* file = nullptr;
  EXPECT_EQ(sdlrdp_drive_open(handle.get(), drive, name, flags, &file), 0) << sdlrdp_last_error();
  return file;
}
void DriveSession::ThenRemovedDrive() {
  std::array<sdlrdp_event, 32> events { };
  auto                         count  = sdlrdp_poll(handle.get(), events.data(), 32);
  EXPECT_TRUE(std::ranges::any_of(std::span(events.data(), count), [&](auto const& event) {
    return event.type == SDLRDP_DRIVE && !event.drive.added && event.drive.id == drive;
  }));
}
void DriveSession::ThenDriveFailure(sdlrdp_file* file, unsigned warnings) {
  sdlrdp_drive value{ };
  EXPECT_EQ(sdlrdp_drive_list(handle.get(), &value, 1), 0);
  EXPECT_EQ(sdlrdp_drive_close(handle.get(), file), -1);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "") - warnings, 1u);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "Drive channel ended: Truncated drive response."), 1u);
}
std::set<std::string> DriveSession::GivenDirectoryEntries() {
  std::set<std::string> expected;
  for (unsigned i = 0; i < 200; ++i) {
    auto name = std::to_string(i);
    expected.insert(name);
    Write("many/" + name, "data");
  }
  return expected;
}
}
