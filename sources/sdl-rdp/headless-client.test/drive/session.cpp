#include <sdl-rdp/headless-client.test/drive/session.hpp>

#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/headless-client.test/backend/config.hpp>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/headless-client.test/drive/share-drive.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/utilities/geometry.hpp>

#include <oxbox/platform/file-writer.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>

namespace sdl_rdp::headless_client_test::drive::detail::session {
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::drive::File;
using sdl_rdp::drive::PeerDisconnected;
using sdl_rdp::headless_client_test::backend::EventsOf;
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::link::DriveChanged;
using sdl_rdp::utilities::Rect;

auto DriveSession::ThenPartialReads(File& file, std::string const& source, std::string& result) -> void {
  for (std::size_t const offset : { 13u, 1048577u, 3145697u }) {
    result.resize(65536);
    auto const count = ReadAt(file, offset, result);
    EXPECT_EQ(result.substr(0, count), source.substr(offset, result.size()));
  }
}
auto DriveSession::SetUp() -> void {
  ASSERT_NO_FATAL_FAILURE(backend.Open(LoopbackConfig(scratch.Path()), logs));
  Connect();
}
auto DriveSession::Connect(std::string const& name, bool second) -> void {
  client = std::make_unique<Client>(backend.Port(), false);
  auto path = scratch.Path().string();
  ShareDrive(*client, path, name);
  if (second) ShareDrive(*client, path, "second");
  ASSERT_TRUE(client->Connect()) << logs.Text(true);
  ASSERT_TRUE(client->Until([&] {
    auto const drives = Files().List();
    if (drives.empty()) return false;
    EXPECT_EQ(drives.front().name, name);
    drive = drives.front().id;
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
  observer                  = std::make_unique<DriveObserver>(*client);
  observer->Observed().hold = true;
}
auto DriveSession::ThenVideoMatches() -> void {
  Pixels     pixels(320uz * 200uz, 0x00446688);
  Rect const damage{ .x = 0, .y = 0, .w = 320, .h = 200 };
  backend.Present(pixels, 320, 200, damage);
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
auto Pattern(std::size_t size, std::uint32_t seed) -> std::string {
  std::string bytes(size, '\0');
  std::ranges::transform(std::views::iota(0uz, size), bytes.begin(),
                         [=](std::size_t i) { return static_cast<char>((i * 31 + i / 251 + seed) & 255); });
  return bytes;
}
auto DriveSession::Write(std::string const& name, std::string const& bytes) -> void {
  oxbox::platform::WriteBinaryFile(scratch.Path() / name, std::as_bytes(std::span(bytes)));
}
auto ReadAt(File& file, std::uint64_t offset, std::span<char> bytes) -> std::size_t {
  return file.Transfer(offset, std::as_writable_bytes(bytes));
}
auto WriteAt(File& file, std::uint64_t offset, std::string_view bytes) -> std::size_t {
  return file.Transfer(offset, std::as_bytes(std::span(bytes)));
}
auto DriveSession::Open(std::string const& name, FileAccess access, FileKind kind) -> OpenedFile {
  return Files().Open(drive, name, access, kind);
}
auto DriveSession::Files() -> DriveFiles {
  return (*backend).Drive();
}
auto DriveSession::ThenRemovedDrive() -> void {
  EXPECT_TRUE(PolledDriveName(false, drive).has_value());
}
auto DriveSession::PolledDriveName(bool added, std::uint32_t id) -> std::optional<std::string> {
  auto const events = EventsOf<DriveChanged>(backend.Poll());
  auto const found  = std::ranges::find_if(
      events, [&](DriveChanged const& event) { return event.added == added && event.id == id; });
  return found == events.end() ? std::nullopt : std::optional<std::string>(found->name);
}
auto DriveSession::ThenDriveFailure(File& file, std::size_t warnings) -> void {
  EXPECT_TRUE(Files().List().empty());
  EXPECT_THROW(file.Close(), PeerDisconnected);
  EXPECT_EQ(logs.Count(LogLevel::Warn, "") - warnings, 1u);
  EXPECT_EQ(logs.Count(LogLevel::Warn, "Drive channel ended: Malformed drive response: truncated."), 1u);
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
