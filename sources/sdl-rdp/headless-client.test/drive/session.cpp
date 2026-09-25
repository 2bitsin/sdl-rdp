#include <sdl-rdp/headless-client.test/drive/session.hpp>
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>

#include <sdl-rdp/headless-client.test/backend/config.hpp>
#include <sdl-rdp/headless-client.test/drive/share-drive.hpp>

#include <oxbox/platform/file-writer.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

namespace sdl_rdp::headless_client_test::drive::detail::session {
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
using sdl_rdp::headless_client_test::client::Pixels;

auto DriveSession::ThenPartialReads(sdlrdp_file& file, std::string const& source, std::string& result) -> void {
  for (std::size_t const offset : { 13u, 1048577u, 3145697u }) {
    result.resize(65536);
    auto count = sdlrdp_drive_read(&*handle, &file, offset, result.data(), result.size());
    ASSERT_GE(count, 0) << sdlrdp_last_error();
    EXPECT_EQ(result.substr(0, count), source.substr(offset, result.size()));
  }
}
auto DriveSession::SetUp() -> void {
  auto path   = scratch.Path().string();
  auto config = LoopbackConfig(path);
  config.log_user = &logs;
  config.log      = Logs::Collect;
  ASSERT_NO_FATAL_FAILURE(handle.Open(config));
  Connect();
}
auto DriveSession::Connect(std::string const& name, bool second) -> void {
  client = std::make_unique<Client>(sdlrdp_port(&*handle), false);
  auto path = scratch.Path().string();
  ShareDrive(*client, path, name);
  if (second) ShareDrive(*client, path, "second");
  ASSERT_TRUE(client->Connect()) << logs.Text(true);
  ASSERT_TRUE(client->Until([&] {
    sdlrdp_drive value{ };
    if (sdlrdp_drive_list(&*handle, &value, 1) != 1) return false;
    EXPECT_EQ(std::string_view(value.name), name);
    drive = value.id;
    return true;
  }));
  pump = PumpInBackground(*client);
}
auto DriveSession::GivenHeldFile() -> void {
  Write("file", "data");
  held_file = Open("file");
  ASSERT_TRUE(held_file.has_value());
  HoldRequests();
}
auto DriveSession::HoldRequests() -> void {
  pump.request_stop();
  pump.join();
  observer                  = std::make_unique<DriveObserver>(*client);
  observer->Observed().hold = true;
}
auto DriveSession::ThenVideoMatches() -> void {
  Pixels            pixels(320uz * 200uz, 0x00446688);
  sdlrdp_rect const damage{ 0, 0, 320, 200 };
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
                         [=](std::size_t i) { return static_cast<char>((i * 31 + i / 251 + seed) & 255); });
  return bytes;
}
auto DriveSession::Write(std::string const& name, std::string const& bytes) -> void {
  oxbox::platform::WriteBinaryFile(scratch.Path() / name, std::as_bytes(std::span(bytes)));
}
auto DriveSession::Open(std::string const& name, std::uint32_t flags) -> OpenedFile {
  sdlrdp_file* file = nullptr;
  EXPECT_EQ(sdlrdp_drive_open(&*handle, drive, name.c_str(), flags, &file), 0) << sdlrdp_last_error();
  return file == nullptr ? OpenedFile{ } : OpenedFile{ *file };
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
auto DriveSession::ThenDriveFailure(sdlrdp_file& file, std::size_t warnings) -> void {
  sdlrdp_drive value{ };
  EXPECT_EQ(sdlrdp_drive_list(&*handle, &value, 1), 0);
  EXPECT_EQ(sdlrdp_drive_close(&*handle, &file), -1);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "") - warnings, 1u);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "Drive channel ended: Malformed drive response: truncated."), 1u);
}
auto DriveSession::GivenDirectoryEntries() -> std::set<std::string> {
  std::set<std::string> expected;
  for (std::size_t i = 0; i < 200; ++i) {
    auto name = std::to_string(i);
    expected.insert(name);
    Write("many/" + name, "data");
  }
  return expected;
}
}
