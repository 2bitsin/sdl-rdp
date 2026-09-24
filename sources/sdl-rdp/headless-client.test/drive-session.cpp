#include <sdl-rdp/headless-client.test/drive-session.hpp>
#include <sdl-rdp/headless-client.test/backend-instance.hpp>

#include <sdl-rdp/headless-client.test/config.hpp>
#include <sdl-rdp/headless-client.test/share-drive.hpp>

#include <oxbox/platform/file-writer.hpp>
#include <algorithm>
#include <array>
#include <ranges>
#include <span>
#include <vector>

namespace DriveGate {
auto DriveSession::ThenPartialReads(sdlrdp_file* file, std::string const& source, std::string& result) -> void {
  for (size_t const offset : { 13u, 1048577u, 3145697u }) {
    result.resize(65536);
    auto count = sdlrdp_drive_read(handle.Handle(), file, offset, result.data(), result.size());
    ASSERT_GE(count, 0) << sdlrdp_last_error();
    EXPECT_EQ(result.substr(0, count), source.substr(offset, result.size()));
  }
}
auto DriveSession::SetUp() -> void {
  auto path   = scratch.Path().string();
  auto config = Headless::LoopbackConfig(path);
  config.log_user = &logs;
  config.log      = Headless::Logs::Collect;
  ASSERT_NO_FATAL_FAILURE(handle.Open(config));
  Connect();
}
auto DriveSession::Connect(char const* name, bool second) -> void {
  client = std::make_unique<Headless::Client>(sdlrdp_port(handle.Handle()), false);
  auto path = scratch.Path().string();
  Headless::ShareDrive(*client, path.c_str(), name);
  if (second) Headless::ShareDrive(*client, path.c_str(), "second");
  ASSERT_TRUE(client->Connect()) << logs.Text(true);
  ASSERT_TRUE(client->Until([&] {
    sdlrdp_drive value{ };
    if (sdlrdp_drive_list(handle.Handle(), &value, 1) != 1) return false;
    EXPECT_STREQ(value.name, name);
    drive = value.id;
    return true;
  }));
  pump = PumpInBackground(*client);
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
  ASSERT_EQ(handle.Present(pixels, 320, 200, damage), 0);
  ASSERT_TRUE(client->Until([&] { return client->Matches(pixels); }));
}
auto DriveSession::Disconnect() -> void {
  pump.request_stop();
  if (pump.joinable()) pump.join();
  if (client) client->Disconnect();
  client.reset();
}
auto DriveSession::TearDown() -> void {
  observer.reset();
  Disconnect();
}
auto DriveSession::Logged(sdlrdp_log_level level, std::string_view text) -> std::size_t {
  return logs.Count(level, text);
}
auto Pattern(std::size_t size, std::uint32_t seed) -> std::string {
  std::string bytes(size, '\0');
  std::ranges::transform(std::views::iota(0uz, size), bytes.begin(),
                         [=](std::size_t i) { return char((i * 31 + i / 251 + seed) & 255); });
  return bytes;
}
auto DriveSession::Write(std::string const& name, std::string const& bytes) -> void {
  oxbox::platform::WriteBinaryFile(scratch.Path() / name, std::as_bytes(std::span(bytes)));
}
auto DriveSession::Open(char const* name, unsigned flags) -> sdlrdp_file* {
  sdlrdp_file* file = nullptr;
  EXPECT_EQ(sdlrdp_drive_open(handle.Handle(), drive, name, flags, &file), 0) << sdlrdp_last_error();
  return file;
}
auto DriveSession::ThenRemovedDrive() -> void {
  EXPECT_TRUE(PolledDriveName(false, drive).has_value());
}
auto DriveSession::PolledDriveName(bool added, std::uint32_t id) -> std::optional<std::string> {
  auto const events = handle.Poll();
  auto const found  = std::ranges::find_if(events, [&](auto const& event) {
    return event.type == SDLRDP_DRIVE && event.drive.added == added && event.drive.id == id;
  });
  return found == events.end() ? std::nullopt : std::optional<std::string>(found->drive.name);
}
auto DriveSession::ThenDriveFailure(sdlrdp_file* file, std::size_t warnings) -> void {
  sdlrdp_drive value{ };
  EXPECT_EQ(sdlrdp_drive_list(handle.Handle(), &value, 1), 0);
  EXPECT_EQ(sdlrdp_drive_close(handle.Handle(), file), -1);
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
