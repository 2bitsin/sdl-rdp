#include "_detail/test-drive.hpp"
namespace DriveGate {
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
}
