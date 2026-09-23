#include "sdl-rdp-backend.h"
#include "_detail/headless-drive.hpp"
#include "_detail/test-io.hpp"
#include "_detail/test-logs.hpp"
#include <oxbox/platform/scratch-area.hpp>
#include <oxbox/platform/file-writer.hpp>
#include <gtest/gtest.h>
#include <future>
#include <thread>
#include <cstring>
#include <climits>
#include <set>
#include <mutex>
#include "_detail/drive-wire.hpp"
#include <freerdp/channels/rdpdr.h>

namespace {
using namespace std::chrono_literals;
class Drive : public testing::Test {
protected:
  Headless::Logs logs;
  oxbox::platform::ScratchArea scratch{"drive", "sdl-rdp"};
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> handle{nullptr, sdlrdp_close};
  std::unique_ptr<Headless::Client> client;
  std::jthread pump;
  unsigned drive = 0;
  void SetUp() override {
    auto path = scratch.Path().string();
    sdlrdp_config config{};
    config.bind = "127.0.0.1"; config.cert_dir = path.c_str();
    config.width = 320; config.height = 200;
    config.log_user = &logs;
    config.log = Headless::Logs::Collect;
    sdlrdp_handle* opened = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &opened), 0) << sdlrdp_last_error();
    handle.reset(opened);
    Connect();
  }
  void Connect(char const* name = "share", bool second = false) {
    client = std::make_unique<Headless::Client>(sdlrdp_port(handle.get()), false);
    auto path = scratch.Path().string();
    Headless::ShareDrive(*client, path.c_str(), name);
    if (second) Headless::ShareDrive(*client, path.c_str(), "second");
    ASSERT_TRUE(freerdp_connect(client->instance.get())) << logs.Text(true);
    ASSERT_TRUE(client->Until([&] {
      sdlrdp_drive value{};
      if (sdlrdp_drive_list(handle.get(), &value, 1) != 1) return false;
      EXPECT_STREQ(value.name, name); drive = value.id;
      return true;
    }));
    pump = std::jthread([&](std::stop_token stop) { while (!stop.stop_requested() && client->Pump()) {} });
  }
  void Disconnect() {
    pump.request_stop();
    if (pump.joinable()) pump.join();
    if (client) freerdp_disconnect(client->instance.get());
    client.reset();
  }
  void TearDown() override { Disconnect(); }
  unsigned Logged(sdlrdp_log_level level, std::string_view text) {
    return logs.Count(level, text);
  }
  static std::string Pattern(size_t size, unsigned seed = 17) {
    std::string bytes(size, '\0');
    for (size_t i = 0; i < size; ++i) bytes[i] = char((i * 31 + i / 251 + seed) & 255);
    return bytes;
  }
  void Write(std::string const& name, std::string const& bytes) {
    oxbox::platform::WriteBinaryFile(scratch.Path() / name, std::as_bytes(std::span(bytes)));
  }
  sdlrdp_file* Open(char const* name, unsigned flags = SDLRDP_FILE_READ) {
    sdlrdp_file* file = nullptr;
    EXPECT_EQ(sdlrdp_drive_open(handle.get(), drive, name, flags, &file), 0) << sdlrdp_last_error();
    return file;
  }
};
Backend::DrivePacket DeviceAnnouncement(unsigned type, unsigned id, std::span<uint8_t const> name) {
  Backend::DrivePacket packet;
  packet.Put(RDPDR_CTYP_CORE, 2); packet.Put(PAKID_CORE_DEVICELIST_ANNOUNCE, 2);
  packet.Put(1); packet.Put(type); packet.Put(id);
  packet.Append(std::array<uint8_t, 8>{'d', 'o', 's', 0, 0, 0, 0, 0});
  packet.Put(name.size()); packet.Append(name);
  return packet;
}
TEST_F(Drive, ReadWriteMetadataAndDirectories) {
  auto source = Pattern(3 * 1024 * 1024);
  Write("disk.img", source);
  auto file = Open("disk.img", SDLRDP_FILE_READ | SDLRDP_FILE_WRITE);
  ASSERT_NE(file, nullptr);
  std::string result(source.size(), '\0');
  auto start = Headless::Clock::now();
  ASSERT_EQ(sdlrdp_drive_read(handle.get(), file, 0, result.data(), result.size()), result.size()) << sdlrdp_last_error();
  auto seconds = std::chrono::duration<double>(Headless::Clock::now() - start).count();
  RecordProperty("read_3MiB_MBps", 3.145728 / seconds);
  EXPECT_EQ(result, source);
  for (size_t offset : {13u, 1048577u, 3145697u}) {
    result.resize(65536);
    auto count = sdlrdp_drive_read(handle.get(), file, offset, result.data(), result.size());
    ASSERT_GE(count, 0) << sdlrdp_last_error();
    EXPECT_EQ(result.substr(0, count), source.substr(offset, result.size()));
  }
  auto pattern = Pattern(1024 * 1024, 29);
  for (auto offset : {0u, 2u * 1024 * 1024}) {
    ASSERT_EQ(sdlrdp_drive_write(handle.get(), file, offset, pattern.data(), pattern.size()), pattern.size());
    source.replace(offset, pattern.size(), pattern);
  }
  EXPECT_EQ(sdlrdp_drive_flush(handle.get(), file), 0);
  EXPECT_EQ(sdlrdp_drive_close(handle.get(), file), 0);
  EXPECT_EQ(Headless::ReadText((scratch.Path() / "disk.img").c_str()), source);
  sdlrdp_stat info{};
  ASSERT_EQ(sdlrdp_drive_stat(handle.get(), drive, "disk.img", &info), 0) << sdlrdp_last_error();
  EXPECT_EQ(info.size, source.size()); EXPECT_FALSE(info.directory); EXPECT_GT(info.modified, 0);
}
TEST_F(Drive, EnumerateAndMutate) {
  ASSERT_EQ(sdlrdp_drive_mkdir(handle.get(), drive, "folder"), 0) << sdlrdp_last_error();
  Write("folder/żółw.txt", "hello"); Write("folder/second", "other");
  sdlrdp_dirent entries[1]{};
  ASSERT_EQ(sdlrdp_drive_enumerate(handle.get(), drive, "folder", 0, entries, 1), 1) << sdlrdp_last_error();
  std::string first = entries[0].name;
  EXPECT_EQ(entries[0].size, 5u);
  ASSERT_EQ(sdlrdp_drive_enumerate(handle.get(), drive, "folder", 1, entries, 1), 1);
  EXPECT_NE(first, entries[0].name);
  EXPECT_EQ(sdlrdp_drive_enumerate(handle.get(), drive, "folder", 2, entries, 1), 0);
  ASSERT_EQ(sdlrdp_drive_rename(handle.get(), drive, "folder/żółw.txt", "folder/renamed"), 0) << sdlrdp_last_error();
  EXPECT_TRUE(std::filesystem::exists(scratch.Path() / "folder/renamed"));
  EXPECT_EQ(sdlrdp_drive_remove(handle.get(), drive, "folder/renamed"), 0) << sdlrdp_last_error();
  EXPECT_EQ(sdlrdp_drive_remove(handle.get(), drive, "folder/second"), 0);
  EXPECT_EQ(sdlrdp_drive_remove(handle.get(), drive, "folder"), 0);
  auto file = reinterpret_cast<sdlrdp_file*>(1);
  EXPECT_EQ(sdlrdp_drive_open(handle.get(), drive, "missing.img", SDLRDP_FILE_READ, &file), -1);
  EXPECT_EQ(file, nullptr);
  EXPECT_NE(std::string(sdlrdp_last_error()).find("missing.img"), std::string::npos);
}
TEST_F(Drive, ConcurrentReadsAndReconnect) {
  std::vector<std::future<bool>> readers;
  for (unsigned i = 0; i < 4; ++i) {
    auto name = std::to_string(i);
    auto source = Pattern(3 * 1024 * 1024, i);
    Write(name, source);
    readers.push_back(std::async(std::launch::async, [&, name, source] {
      sdlrdp_file* file = nullptr;
      if (sdlrdp_drive_open(handle.get(), drive, name.c_str(), SDLRDP_FILE_READ, &file) < 0) return false;
      std::string bytes(source.size(), '\0');
      auto count = sdlrdp_drive_read(handle.get(), file, 0, bytes.data(), bytes.size());
      sdlrdp_drive_close(handle.get(), file);
      return count == int(source.size()) && bytes == source;
    }));
  }
  for (auto& result : readers) { ASSERT_EQ(result.wait_for(10s), std::future_status::ready); EXPECT_TRUE(result.get()); }
  auto file = Open("0");
  auto old = drive;
  Disconnect();
  std::string bytes(1024, '\0');
  EXPECT_EQ(sdlrdp_drive_read(handle.get(), file, 0, bytes.data(), bytes.size()), -1);
  EXPECT_NE(std::string(sdlrdp_last_error()).find("disconnect"), std::string::npos);
  EXPECT_EQ(sdlrdp_drive_close(handle.get(), file), -1);
  Connect();
  EXPECT_NE(drive, old);
}
TEST_F(Drive, DisconnectDuringRead) {
  Write("large", Pattern(3 * 1024 * 1024));
  auto file = Open("large");
  pump.request_stop(); pump.join();
  auto read = std::async(std::launch::async, [&] {
    std::string bytes(3 * 1024 * 1024, '\0');
    auto result = sdlrdp_drive_read(handle.get(), file, 0, bytes.data(), bytes.size());
    return std::pair(result, std::string(sdlrdp_last_error()));
  });
  std::this_thread::sleep_for(20ms);
  Disconnect();
  ASSERT_EQ(read.wait_for(2s), std::future_status::ready);
  auto [result, error] = read.get();
  EXPECT_EQ(result, -1); EXPECT_NE(error.find("disconnect"), std::string::npos);
  EXPECT_EQ(sdlrdp_drive_close(handle.get(), file), -1);
  sdlrdp_drive value{};
  EXPECT_EQ(sdlrdp_drive_list(handle.get(), &value, 1), 0);
  Connect();
}
TEST_F(Drive, SparseOffsetAboveFourGiB) {
  auto file = Open("sparse", SDLRDP_FILE_READ | SDLRDP_FILE_WRITE | SDLRDP_FILE_CREATE);
  ASSERT_NE(file, nullptr);
  constexpr uint64_t offset = (uint64_t(1) << 32) + 123;
  EXPECT_EQ(sdlrdp_drive_write(handle.get(), file, offset, "high", 4), 4);
  char bytes[4]{};
  EXPECT_EQ(sdlrdp_drive_read(handle.get(), file, offset, bytes, 4), 4);
  EXPECT_EQ(std::string(bytes, 4), "high");
  sdlrdp_stat info{};
  EXPECT_EQ(sdlrdp_drive_fstat(handle.get(), file, &info), 0);
  EXPECT_EQ(info.size, offset + 4);
  EXPECT_EQ(sdlrdp_drive_close(handle.get(), file), 0);
}

TEST_F(Drive, AnnounceAndRemoveEvents) {
  sdlrdp_event events[32]{};
  auto count = sdlrdp_poll(handle.get(), events, 32);
  auto added = std::ranges::find_if(std::span(events, count), [&](auto const& event) {
    return event.type == SDLRDP_DRIVE && event.drive.added && event.drive.id == drive;
  });
  ASSERT_NE(added, std::span(events, count).end());
  EXPECT_STREQ(added->drive.name, "share");
  auto old = drive;
  Disconnect();
  auto deadline = Headless::Clock::now() + 2s;
  sdlrdp_drive value{};
  while (sdlrdp_drive_list(handle.get(), &value, 1) && Headless::Clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  EXPECT_EQ(sdlrdp_drive_list(handle.get(), &value, 1), 0);
  count = sdlrdp_poll(handle.get(), events, 32);
  auto removed = std::ranges::find_if(std::span(events, count), [&](auto const& event) {
    return event.type == SDLRDP_DRIVE && !event.drive.added && event.drive.id == old;
  });
  ASSERT_NE(removed, std::span(events, count).end());
  EXPECT_STREQ(removed->drive.name, "share");
  Connect();
  EXPECT_NE(drive, old);
}

TEST_F(Drive, RejectsFileDirectoryMismatchWithClientStatus) {
  Write("file", "data");
  ASSERT_EQ(sdlrdp_drive_mkdir(handle.get(), drive, "directory"), 0);
  sdlrdp_file* file = nullptr;
  EXPECT_EQ(sdlrdp_drive_open(handle.get(), drive, "directory", SDLRDP_FILE_READ, &file), -1);
  EXPECT_EQ(file, nullptr);
  EXPECT_NE(std::string(sdlrdp_last_error()).find("STATUS_ACCESS_DENIED (0xc0000022)"), std::string::npos);
  EXPECT_EQ(sdlrdp_drive_open(handle.get(), drive, "file", SDLRDP_FILE_DIRECTORY, &file), -1);
  EXPECT_EQ(file, nullptr);
  EXPECT_NE(std::string(sdlrdp_last_error()).find("STATUS_NOT_A_DIRECTORY (0xc0000103)"), std::string::npos);
}
TEST_F(Drive, ClientFailureStatusNames) {
  pump.request_stop(); pump.join();
  Headless::DriveObserver observer(*client);
  observer.hold = true;
  const std::pair<uint32_t, char const*> cases[]{
    {STATUS_NO_SUCH_FILE, "STATUS_NO_SUCH_FILE (0xc000000f)"},
    {STATUS_OBJECT_NAME_NOT_FOUND, "STATUS_OBJECT_NAME_NOT_FOUND (0xc0000034)"},
    {STATUS_OBJECT_PATH_NOT_FOUND, "STATUS_OBJECT_PATH_NOT_FOUND (0xc000003a)"},
    {STATUS_ACCESS_DENIED, "STATUS_ACCESS_DENIED (0xc0000022)"},
    {STATUS_OBJECT_NAME_COLLISION, "STATUS_OBJECT_NAME_COLLISION (0xc0000035)"},
    {STATUS_NOT_A_DIRECTORY, "STATUS_NOT_A_DIRECTORY (0xc0000103)"},
    {STATUS_FILE_IS_A_DIRECTORY, "STATUS_FILE_IS_A_DIRECTORY (0xc00000ba)"},
    {STATUS_DIRECTORY_NOT_EMPTY, "STATUS_DIRECTORY_NOT_EMPTY (0xc0000101)"},
    {STATUS_DISK_FULL, "STATUS_DISK_FULL (0xc000007f)"},
    {STATUS_SHARING_VIOLATION, "STATUS_SHARING_VIOLATION (0xc0000043)"},
    {STATUS_NOT_SUPPORTED, "STATUS_NOT_SUPPORTED (0xc00000bb)"},
    {STATUS_UNSUCCESSFUL, "STATUS_UNSUCCESSFUL (0xc0000001)"},
    {0xdeadbeef, "NTSTATUS 0xdeadbeef"},
  };
  for (auto [status, text] : cases) {
    SCOPED_TRACE(text);
    observer.io.clear();
    auto open = std::async(std::launch::async, [&] {
      sdlrdp_file* file = nullptr;
      auto result = sdlrdp_drive_open(handle.get(), drive, "missing.bin", SDLRDP_FILE_READ, &file);
      return std::pair(result, std::string(sdlrdp_last_error()));
    });
    ASSERT_TRUE(client->Until([&] { return !observer.io.empty(); }));
    auto request = observer.io.front();
    auto device = request.Get(4); request.Skip(4);
    auto id = request.Get(4);
    Backend::DrivePacket response;
    response.Put(RDPDR_CTYP_CORE, 2); response.Put(PAKID_CORE_DEVICE_IOCOMPLETION, 2);
    response.Put(device); response.Put(id); response.Put(status);
    ASSERT_TRUE(observer.Send(response));
    ASSERT_TRUE(client->Until([&] { return open.wait_for(0s) == std::future_status::ready; }));
    auto [result, error] = open.get();
    EXPECT_EQ(result, -1);
    EXPECT_EQ(error, std::string("Drive 'missing.bin' failed: ") + text);
  }
}
TEST_F(Drive, OversizedReadSendsNoRequest) {
  Write("file", "data");
  auto file = Open("file");
  ASSERT_NE(file, nullptr);
  pump.request_stop(); pump.join();
  {
  Headless::DriveObserver observer(*client);
  char byte{};
  EXPECT_EQ(sdlrdp_drive_flush(handle.get(), file), 0);
  EXPECT_EQ(sdlrdp_drive_read(handle.get(), file, 0, &byte, size_t(INT_MAX) + 1), -1);
  EXPECT_NE(std::string(sdlrdp_last_error()).find("Invalid drive transfer"), std::string::npos);
  for (unsigned i = 0; i < 10; ++i) ASSERT_TRUE(client->Pump());
  EXPECT_EQ(observer.requests, 0u);
  }
  pump = std::jthread([&](std::stop_token stop) { while (!stop.stop_requested() && client->Pump()) {} });
  EXPECT_EQ(sdlrdp_drive_close(handle.get(), file), 0);
}
TEST_F(Drive, TwoSharesIncludingUnicodeName) {
  Disconnect();
  Connect("żółw", true);
  Write("file", "data");
  sdlrdp_drive drives[2]{};
  auto deadline = Headless::Clock::now() + 2s;
  while (sdlrdp_drive_list(handle.get(), drives, 2) != 2 && Headless::Clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  ASSERT_EQ(sdlrdp_drive_list(handle.get(), drives, 2), 2);
  EXPECT_EQ((std::set<std::string>{drives[0].name, drives[1].name}), (std::set<std::string>{"żółw", "second"}));
  EXPECT_NE(drives[0].id, drives[1].id);
  for (auto const& entry : drives) {
    sdlrdp_file* file = nullptr;
    ASSERT_EQ(sdlrdp_drive_open(handle.get(), entry.id, "file", SDLRDP_FILE_READ, &file), 0);
    char bytes[4]{};
    EXPECT_EQ(sdlrdp_drive_read(handle.get(), file, 0, bytes, sizeof(bytes)), 4);
    EXPECT_EQ(std::string(bytes, sizeof(bytes)), "data");
    EXPECT_EQ(sdlrdp_drive_close(handle.get(), file), 0);
  }
}
TEST_F(Drive, TwoHundredEntriesInPagesOfThirtyTwo) {
  ASSERT_EQ(sdlrdp_drive_mkdir(handle.get(), drive, "many"), 0);
  std::set<std::string> expected, actual;
  for (unsigned i = 0; i < 200; ++i) {
    auto name = std::to_string(i);
    expected.insert(name);
    Write("many/" + name, "data");
  }
  sdlrdp_dirent entries[32]{};
  unsigned offset = 0;
  for (;;) {
    auto count = sdlrdp_drive_enumerate(handle.get(), drive, "many", offset, entries, 32);
    ASSERT_GE(count, 0) << sdlrdp_last_error();
    for (int i = 0; i < count; ++i) EXPECT_TRUE(actual.insert(entries[i].name).second);
    offset += count;
    if (count < 32) break;
    ASSERT_LE(offset, 200u);
  }
  EXPECT_EQ(offset, 200u);
  EXPECT_EQ(actual, expected);
}
TEST_F(Drive, MalformedChannelKeepsVideoSession) {
  Write("file", Pattern(3 * 1024 * 1024));
  auto file = Open("file");
  ASSERT_NE(file, nullptr);
  pump.request_stop(); pump.join();
  Headless::DriveObserver observer(*client);
  observer.hold = true;
  auto read = std::async(std::launch::async, [&] {
    std::string bytes(3 * 1024 * 1024, '\0');
    return sdlrdp_drive_read(handle.get(), file, 0, bytes.data(), bytes.size());
  });
  ASSERT_TRUE(client->Until([&] { return observer.requests == 8; }));
  auto instance = client->instance.get();
  auto channel = freerdp_channels_get_id_by_name(instance, RDPDR_CHANNEL_NAME);
  BYTE malformed[]{0x72, 0x44, 0x41, 0x44};
  ASSERT_TRUE(instance->SendChannelData(instance, channel, malformed, sizeof(malformed)));
  ASSERT_TRUE(client->Until([&] {
    sdlrdp_drive value{};
    return sdlrdp_drive_list(handle.get(), &value, 1) == 0;
  }));
  ASSERT_EQ(read.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(read.get(), -1);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "Drive channel ended: Truncated drive response."), 1u);
  EXPECT_EQ(sdlrdp_drive_close(handle.get(), file), -1);
  sdlrdp_event events[32]{};
  auto count = sdlrdp_poll(handle.get(), events, 32);
  EXPECT_TRUE(std::ranges::any_of(std::span(events, count), [&](auto const& event) {
    return event.type == SDLRDP_DRIVE && !event.drive.added && event.drive.id == drive;
  }));
  std::vector<UINT32> pixels(320 * 200, 0x00446688);
  sdlrdp_rect damage{0, 0, 320, 200};
  ASSERT_EQ(sdlrdp_present(handle.get(), pixels.data(), 320 * 4, 320, 200, &damage, 1), 0);
  ASSERT_TRUE(client->Until([&] { return client->Matches(pixels); }));
}

TEST_F(Drive, MalformedInformationKeepsVideoSession) {
  Write("file", "data");
  auto file = Open("file");
  ASSERT_NE(file, nullptr);
  pump.request_stop(); pump.join();
  Headless::DriveObserver observer(*client);
  observer.hold = true;
  auto stat = std::async(std::launch::async, [&] {
    sdlrdp_stat info{};
    auto result = sdlrdp_drive_fstat(handle.get(), file, &info);
    return std::pair(result, std::string(sdlrdp_last_error()));
  });
  ASSERT_TRUE(client->Until([&] { return observer.requests == 1; }));
  auto request = observer.io.front();
  auto device = request.Get(4); request.Skip(4);
  auto id = request.Get(4);
  EXPECT_EQ(request.Get(4), IRP_MJ_QUERY_INFORMATION); request.Skip(4);
  EXPECT_EQ(request.Get(4), FileBasicInformation);
  Backend::DrivePacket response;
  response.Put(RDPDR_CTYP_CORE, 2); response.Put(PAKID_CORE_DEVICE_IOCOMPLETION, 2);
  response.Put(device); response.Put(id); response.Put(STATUS_SUCCESS);
  response.Put(0);
  auto warnings = Logged(SDLRDP_LOG_WARN, "");
  ASSERT_TRUE(observer.Send(response));
  ASSERT_TRUE(client->Until([&] { return stat.wait_for(0s) == std::future_status::ready; }));
  auto [result, error] = stat.get();
  EXPECT_EQ(result, -1); EXPECT_EQ(error, "Truncated drive response.");
  sdlrdp_drive value{};
  EXPECT_EQ(sdlrdp_drive_list(handle.get(), &value, 1), 0);
  EXPECT_EQ(sdlrdp_drive_close(handle.get(), file), -1);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "") - warnings, 1u);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "Drive channel ended: Truncated drive response."), 1u);
  std::vector<UINT32> pixels(320 * 200, 0x00446688);
  sdlrdp_rect damage{0, 0, 320, 200};
  ASSERT_EQ(sdlrdp_present(handle.get(), pixels.data(), 1280, 320, 200, &damage, 1), 0);
  ASSERT_TRUE(client->Until([&] { return client->Matches(pixels); }));
  RecordProperty("trace", "FileBasicInformation: Length=0; fstat=-1; Truncated drive response.; "
    "drives=0; WARN=1: Drive channel ended: Truncated drive response.; video matches");
}

TEST_F(Drive, SlidingWindowRefillsOnOutOfOrderCompletion) {
  Write("file", "data");
  auto file = Open("file");
  ASSERT_NE(file, nullptr);
  pump.request_stop(); pump.join();
  Headless::DriveObserver observer(*client);
  observer.hold = true;
  std::string bytes(10 * 65536, '\0');
  auto read = std::async(std::launch::async, [&] {
    return sdlrdp_drive_read(handle.get(), file, 0, bytes.data(), bytes.size());
  });
  ASSERT_TRUE(client->Until([&] { return observer.requests == 8; }));
  auto complete = [&](size_t index) {
    auto request = observer.io[index];
    auto device = request.Get(4);
    request.Skip(4);
    auto id = request.Get(4);
    Backend::DrivePacket response;
    response.Put(RDPDR_CTYP_CORE, 2); response.Put(PAKID_CORE_DEVICE_IOCOMPLETION, 2);
    response.Put(device); response.Put(id); response.Put(STATUS_SUCCESS);
    response.Put(65536); response.bytes.resize(response.bytes.size() + 65536, 'x');
    EXPECT_TRUE(observer.Send(response));
  };
  complete(7);
  ASSERT_TRUE(client->Until([&] { return observer.requests == 9; }));
  complete(0);
  ASSERT_TRUE(client->Until([&] { return observer.requests == 10; }));
  for (size_t index = 1; index < 10; ++index) if (index != 7) complete(index);
  ASSERT_TRUE(client->Until([&] { return read.wait_for(0s) == std::future_status::ready; }));
  EXPECT_EQ(read.get(), int(bytes.size()));
  EXPECT_EQ(bytes, std::string(bytes.size(), 'x'));
  observer.hold = false;
  auto close = std::async(std::launch::async, [&] { return sdlrdp_drive_close(handle.get(), file); });
  ASSERT_TRUE(client->Until([&] { return close.wait_for(0s) == std::future_status::ready; }));
  EXPECT_EQ(close.get(), 0);
}

TEST_F(Drive, UnicodeWireNameAndRecoverableAnnouncements) {
  pump.request_stop(); pump.join();
  Headless::DriveObserver observer(*client);
  observer.hold = true;
  std::string_view label = "żółw";
  auto wide = Backend::TranscodeRange<std::vector<uint8_t>>(std::as_bytes(std::span(label)), {},
    {oxbox::utilities::Encoding::UTF16, std::endian::little});
  wide.resize(wide.size() + 2);
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_FILESYSTEM, 100, wide)));
  std::vector<uint8_t> long_name(600, 'x'); long_name.push_back(0);
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_FILESYSTEM, 101, long_name)));
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_FILESYSTEM, 102, std::array<uint8_t, 2>{0xff, 0})));
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_PRINT, 103, {})));
  ASSERT_TRUE(client->Until([&] {
    return std::ranges::any_of(observer.replies, [](auto const& reply) { return reply.first == 103; });
  }));
  sdlrdp_drive drives[8]{};
  ASSERT_EQ(sdlrdp_drive_list(handle.get(), drives, 8), 4);
  EXPECT_STREQ(drives[1].name, "żółw");
  EXPECT_EQ(std::string(drives[2].name), std::string(511, 'x'));
  EXPECT_STREQ(drives[3].name, "dos");
  auto rejected = std::ranges::find(observer.replies, 103u, &std::pair<unsigned, unsigned>::first);
  ASSERT_NE(rejected, observer.replies.end());
  EXPECT_EQ(rejected->second, STATUS_NOT_SUPPORTED);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "truncating"), 1u);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "Using DOS name"), 1u);
  EXPECT_EQ(Logged(SDLRDP_LOG_INFO, "extended PDU"), 1u);
}
TEST_F(Drive, UnknownCompletionIsIgnored) {
  pump.request_stop(); pump.join();
  Headless::DriveObserver observer(*client);
  Backend::DrivePacket packet;
  packet.Put(RDPDR_CTYP_CORE, 2); packet.Put(PAKID_CORE_DEVICE_IOCOMPLETION, 2);
  packet.Put(0); packet.Put(UINT32_MAX); packet.Put(STATUS_SUCCESS);
  ASSERT_TRUE(observer.Send(packet));
  ASSERT_TRUE(client->Until([&] { return Logged(SDLRDP_LOG_WARN, "Unknown drive completion id") == 1; }));
  sdlrdp_drive value{};
  EXPECT_EQ(sdlrdp_drive_list(handle.get(), &value, 1), 1);
  std::vector<UINT32> pixels(320 * 200, 0x00446688);
  sdlrdp_rect damage{0, 0, 320, 200};
  ASSERT_EQ(sdlrdp_present(handle.get(), pixels.data(), 1280, 320, 200, &damage, 1), 0);
  ASSERT_TRUE(client->Until([&] { return client->Matches(pixels); }));
}

}
