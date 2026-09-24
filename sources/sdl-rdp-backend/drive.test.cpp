#include <sdl-rdp/headless-client.test/drive-checks.hpp>
#include <sdl-rdp/headless-client.test/io.hpp>
#include <sdl-rdp/headless-client.test/rdpdr-packets.hpp>

#include <cstddef>
#include <future>
#include <utility>
namespace DriveGate {
namespace {
auto ReadSharedFile(sdlrdp_handle* handle, unsigned drive, std::string const& name, std::string const& source) -> bool {
  sdlrdp_file* file = nullptr;
  if (sdlrdp_drive_open(handle, drive, name.c_str(), SDLRDP_FILE_READ, &file) < 0) return false;
  std::string bytes(source.size(), '\0');
  auto        count = sdlrdp_drive_read(handle, file, 0, bytes.data(), bytes.size());
  sdlrdp_drive_close(handle, file);
  return std::cmp_equal(count, source.size()) && bytes == source;
}
auto ThenReaders(std::span<std::future<bool>> readers) -> void {
  for (auto& result : readers) {
    ASSERT_EQ(result.wait_for(10s), std::future_status::ready);
    EXPECT_TRUE(result.get());
  }
}
std::array<std::pair<uint32_t, char const*>, 13> constexpr FailureStatusNames{ {
    { STATUS_NO_SUCH_FILE         , "STATUS_NO_SUCH_FILE (0xc000000f)"          },
    { STATUS_OBJECT_NAME_NOT_FOUND, "STATUS_OBJECT_NAME_NOT_FOUND (0xc0000034)" },
    { STATUS_OBJECT_PATH_NOT_FOUND, "STATUS_OBJECT_PATH_NOT_FOUND (0xc000003a)" },
    { STATUS_ACCESS_DENIED        , "STATUS_ACCESS_DENIED (0xc0000022)"         },
    { STATUS_OBJECT_NAME_COLLISION, "STATUS_OBJECT_NAME_COLLISION (0xc0000035)" },
    { STATUS_NOT_A_DIRECTORY      , "STATUS_NOT_A_DIRECTORY (0xc0000103)"       },
    { STATUS_FILE_IS_A_DIRECTORY  , "STATUS_FILE_IS_A_DIRECTORY (0xc00000ba)"   },
    { STATUS_DIRECTORY_NOT_EMPTY  , "STATUS_DIRECTORY_NOT_EMPTY (0xc0000101)"   },
    { STATUS_DISK_FULL            , "STATUS_DISK_FULL (0xc000007f)"             },
    { STATUS_SHARING_VIOLATION    , "STATUS_SHARING_VIOLATION (0xc0000043)"     },
    { STATUS_NOT_SUPPORTED        , "STATUS_NOT_SUPPORTED (0xc00000bb)"         },
    { STATUS_UNSUCCESSFUL         , "STATUS_UNSUCCESSFUL (0xc0000001)"          },
    { 0xdeadbeef                  , "NTSTATUS 0xdeadbeef"                       },
} };
}

namespace {
auto ThenMissingDriveFile(sdlrdp_handle* handle, unsigned drive) -> void {
  auto* file = reinterpret_cast<sdlrdp_file*>(1);
  EXPECT_EQ(sdlrdp_drive_open(handle, drive, "missing.img", SDLRDP_FILE_READ, &file), -1);
  EXPECT_EQ(file, nullptr);
  EXPECT_NE(std::string(sdlrdp_last_error()).find("missing.img"), std::string::npos);
}
}
namespace {
class Drive : public DriveChecks {
protected:
  auto WhenDirectoryPaged(std::array<sdlrdp_dirent, 32>& entries, std::set<std::string>& actual, unsigned& offset)
      -> void {
    for (;;) {
      auto count = sdlrdp_drive_enumerate(handle.Handle(), drive, "many", offset, entries.data(), 32);
      ASSERT_GE(count, 0) << sdlrdp_last_error();
      ASSERT_NO_FATAL_FAILURE(ThenUniquePage(std::span(entries).first(count), actual));
      offset += count;
      if (count < 32) break;
      ASSERT_LE(offset, 200u);
    }
  }
  auto ThenRemovedEvent(std::uint32_t old) -> void {
    auto         deadline = Headless::Clock::now() + 2s;
    sdlrdp_drive value    { };
    while (sdlrdp_drive_list(handle.Handle(), &value, 1) && Headless::Clock::now() < deadline)
      std::this_thread::sleep_for(1ms);
    EXPECT_EQ(sdlrdp_drive_list(handle.Handle(), &value, 1), 0);
    EXPECT_EQ(PolledDriveName(false, old), "share");
  }
  auto ThenFileMetadata(std::string const& source) -> void {
    sdlrdp_stat info{ };
    ASSERT_EQ(sdlrdp_drive_stat(handle.Handle(), drive, "disk.img", &info), 0) << sdlrdp_last_error();
    EXPECT_EQ(info.size, source.size());
    EXPECT_FALSE(info.directory);
    EXPECT_GT(info.modified, 0);
  }
  auto ThenNoDriveRequests(Headless::DriveObserver const& observer) -> void {
    for (unsigned i = 0; i < 10; ++i) ASSERT_TRUE(client->Pump());
    EXPECT_EQ(observer.Observed().requests, 0u);
  }
  static auto ThenUniquePage(std::span<sdlrdp_dirent const> entries, std::set<std::string>& actual) -> void {
    for (auto const& entry : entries) EXPECT_TRUE(actual.insert(entry.name).second);
  }
  auto ThenSharedFile(sdlrdp_drive const& entry) -> void {
    sdlrdp_file* file = nullptr;
    ASSERT_EQ(sdlrdp_drive_open(handle.Handle(), entry.id, "file", SDLRDP_FILE_READ, &file), 0);
    std::array<char, 4> bytes{ };
    EXPECT_EQ(sdlrdp_drive_read(handle.Handle(), file, 0, bytes.data(), bytes.size()), 4);
    EXPECT_EQ(std::string(bytes.data(), bytes.size()), "data");
    EXPECT_EQ(sdlrdp_drive_close(handle.Handle(), file), 0);
  }
  auto ThenOversizedRead(sdlrdp_file* file, Headless::DriveObserver const& observer) -> void {
    char byte{ };
    EXPECT_EQ(sdlrdp_drive_flush(handle.Handle(), file), 0);
    EXPECT_EQ(sdlrdp_drive_read(handle.Handle(), file, 0, &byte, std::size_t{ INT_MAX } + 1), -1);
    EXPECT_NE(std::string(sdlrdp_last_error()).find("Invalid drive transfer"), std::string::npos);
    ASSERT_NO_FATAL_FAILURE(ThenNoDriveRequests(observer));
  }
  auto ThenClientFailure(Headless::DriveObserver& observer, uint32_t status, char const* text) -> void {
    SCOPED_TRACE(text);
    observer.Observed().io.clear();
    auto open = std::async(std::launch::async, [&] {
      sdlrdp_file* file   = nullptr;
      auto         result = sdlrdp_drive_open(handle.Handle(), drive, "missing.bin", SDLRDP_FILE_READ, &file);
      return std::pair(result, std::string(sdlrdp_last_error()));
    });
    ASSERT_TRUE(client->Until([&] { return !observer.Observed().io.empty(); }));
    auto response = ReplyTo(observer.Observed().io.front(), status);
    ASSERT_TRUE(observer.Send(response));
    ASSERT_TRUE(client->Until([&] { return open.wait_for(0s) == std::future_status::ready; }));
    auto [result, error] = open.get();
    EXPECT_EQ(result, -1);
    EXPECT_EQ(error, std::string("Drive 'missing.bin' failed: ") + text);
  }
  auto ThenDirectoryOpenRejected(sdlrdp_file*& file) -> void {
    EXPECT_EQ(sdlrdp_drive_open(handle.Handle(), drive, "file", SDLRDP_FILE_DIRECTORY, &file), -1);
    EXPECT_EQ(file, nullptr);
    EXPECT_NE(std::string(sdlrdp_last_error()).find("STATUS_NOT_A_DIRECTORY (0xc0000103)"), std::string::npos);
  }
  auto ThenSparseSize(sdlrdp_file* file, uint64_t offset) -> void {
    sdlrdp_stat info{ };
    EXPECT_EQ(sdlrdp_drive_fstat(handle.Handle(), file, &info), 0);
    EXPECT_EQ(info.size, offset + 4);
    EXPECT_EQ(sdlrdp_drive_close(handle.Handle(), file), 0);
  }
  auto WhenFileRewritten(sdlrdp_file* file, std::string& source) -> void {
    auto pattern = Pattern(1024uz * 1024, 29);
    for (auto offset : { 0uz, 2uz * 1024 * 1024 }) {
      ASSERT_EQ(sdlrdp_drive_write(handle.Handle(), file, offset, pattern.data(), pattern.size()), pattern.size());
      source.replace(offset, pattern.size(), pattern);
    }
    EXPECT_EQ(sdlrdp_drive_flush(handle.Handle(), file), 0);
    EXPECT_EQ(sdlrdp_drive_close(handle.Handle(), file), 0);
    EXPECT_EQ(Headless::ReadText((scratch.Path() / "disk.img").c_str()), source);
  }
};
TEST_F(Drive, ReadWriteMetadataAndDirectories) {
  auto source = Pattern(static_cast<std::ptrdiff_t>(3 * 1024) * 1024);
  Write("disk.img", source);
  auto* file = Open("disk.img", SDLRDP_FILE_READ | SDLRDP_FILE_WRITE);
  ASSERT_NE(file, nullptr);
  ASSERT_NO_FATAL_FAILURE(ThenReadRanges(file, source));
  ASSERT_NO_FATAL_FAILURE(WhenFileRewritten(file, source));
  ASSERT_NO_FATAL_FAILURE(ThenFileMetadata(source));
}
TEST_F(Drive, EnumerateAndMutate) {
  ASSERT_EQ(sdlrdp_drive_mkdir(handle.Handle(), drive, "folder"), 0) << sdlrdp_last_error();
  Write("folder/żółw.txt", "hello");
  Write("folder/second", "other");
  std::array<sdlrdp_dirent, 1> entries{ };
  ASSERT_EQ(sdlrdp_drive_enumerate(handle.Handle(), drive, "folder", 0, entries.data(), 1), 1) << sdlrdp_last_error();
  std::string const first = entries[0].name;
  EXPECT_EQ(entries[0].size, 5u);
  ASSERT_EQ(sdlrdp_drive_enumerate(handle.Handle(), drive, "folder", 1, entries.data(), 1), 1);
  EXPECT_NE(first, entries[0].name);
  EXPECT_EQ(sdlrdp_drive_enumerate(handle.Handle(), drive, "folder", 2, entries.data(), 1), 0);
  ASSERT_EQ(sdlrdp_drive_rename(handle.Handle(), drive, "folder/żółw.txt", "folder/renamed"), 0) << sdlrdp_last_error();
  EXPECT_TRUE(std::filesystem::exists(scratch.Path() / "folder/renamed"));
  EXPECT_EQ(sdlrdp_drive_remove(handle.Handle(), drive, "folder/renamed"), 0) << sdlrdp_last_error();
  EXPECT_EQ(sdlrdp_drive_remove(handle.Handle(), drive, "folder/second"), 0);
  EXPECT_EQ(sdlrdp_drive_remove(handle.Handle(), drive, "folder"), 0);
  ThenMissingDriveFile(handle.Handle(), drive);
}
TEST_F(Drive, ConcurrentReadsAndReconnect) {
  std::vector<std::future<bool>> readers;
  for (unsigned i = 0; i < 4; ++i) {
    auto name   = std::to_string(i);
    auto source = Pattern(static_cast<std::ptrdiff_t>(3 * 1024) * 1024, i);
    Write(name, source);
    readers.push_back(std::async(std::launch::async,
                                 [&, name, source] { return ReadSharedFile(handle.Handle(), drive, name, source); }));
  }
  ASSERT_NO_FATAL_FAILURE(ThenReaders(readers));
  auto* file = Open("0");
  auto  old  = drive;
  Disconnect();
  std::string bytes(1024, '\0');
  EXPECT_EQ(sdlrdp_drive_read(handle.Handle(), file, 0, bytes.data(), bytes.size()), -1);
  EXPECT_NE(std::string(sdlrdp_last_error()).find("disconnect"), std::string::npos);
  EXPECT_EQ(sdlrdp_drive_close(handle.Handle(), file), -1);
  ASSERT_NO_FATAL_FAILURE(Connect());
  EXPECT_NE(drive, old);
}
TEST_F(Drive, DisconnectDuringRead) {
  Write("large", Pattern(static_cast<std::ptrdiff_t>(3 * 1024) * 1024));
  auto* file = Open("large");
  pump.request_stop();
  pump.join();
  auto read = std::async(std::launch::async, [&] {
    std::string bytes(static_cast<std::ptrdiff_t>(3 * 1024) * 1024, '\0');
    auto        result = sdlrdp_drive_read(handle.Handle(), file, 0, bytes.data(), bytes.size());
    return std::pair(result, std::string(sdlrdp_last_error()));
  });
  std::this_thread::sleep_for(20ms);
  Disconnect();
  ASSERT_EQ(read.wait_for(2s), std::future_status::ready);
  auto [result, error] = read.get();
  EXPECT_EQ(result, -1);
  EXPECT_NE(error.find("disconnect"), std::string::npos);
  EXPECT_EQ(sdlrdp_drive_close(handle.Handle(), file), -1);
  sdlrdp_drive value{ };
  EXPECT_EQ(sdlrdp_drive_list(handle.Handle(), &value, 1), 0);
  Connect();
}
TEST_F(Drive, SparseOffsetAboveFourGiB) {
  auto* file = Open("sparse", SDLRDP_FILE_READ | SDLRDP_FILE_WRITE | SDLRDP_FILE_CREATE);
  ASSERT_NE(file, nullptr);
  constexpr uint64_t offset = (uint64_t(1) << 32) + 123;
  EXPECT_EQ(sdlrdp_drive_write(handle.Handle(), file, offset, "high", 4), 4);
  std::array<char, 4> bytes{ };
  EXPECT_EQ(sdlrdp_drive_read(handle.Handle(), file, offset, bytes.data(), 4), 4);
  EXPECT_EQ(std::string(bytes.data(), 4), "high");
  ThenSparseSize(file, offset);
}

TEST_F(Drive, AnnounceAndRemoveEvents) {
  EXPECT_EQ(PolledDriveName(true, drive), "share");
  auto old = drive;
  Disconnect();
  ASSERT_NO_FATAL_FAILURE(ThenRemovedEvent(old));
  ASSERT_NO_FATAL_FAILURE(Connect());
  EXPECT_NE(drive, old);
}

TEST_F(Drive, RejectsFileDirectoryMismatchWithClientStatus) {
  Write("file", "data");
  ASSERT_EQ(sdlrdp_drive_mkdir(handle.Handle(), drive, "directory"), 0);
  sdlrdp_file* file = nullptr;
  EXPECT_EQ(sdlrdp_drive_open(handle.Handle(), drive, "directory", SDLRDP_FILE_READ, &file), -1);
  EXPECT_EQ(file, nullptr);
  EXPECT_NE(std::string(sdlrdp_last_error()).find("STATUS_ACCESS_DENIED (0xc0000022)"), std::string::npos);
  ThenDirectoryOpenRejected(file);
}
TEST_F(Drive, ClientFailureStatusNames) {
  ASSERT_NO_FATAL_FAILURE(HoldRequests());
  auto& observer = *this->observer;
  for (auto [status, text] : FailureStatusNames) {
    ASSERT_NO_FATAL_FAILURE(ThenClientFailure(observer, status, text));
  }
}
TEST_F(Drive, OversizedReadSendsNoRequest) {
  Write("file", "data");
  auto* file = Open("file");
  ASSERT_NE(file, nullptr);
  pump.request_stop();
  pump.join();
  {
    Headless::DriveObserver const observer(*client);
    ASSERT_NO_FATAL_FAILURE(ThenOversizedRead(file, observer));
  }
  pump = PumpInBackground(*client);
  EXPECT_EQ(sdlrdp_drive_close(handle.Handle(), file), 0);
}
TEST_F(Drive, TwoSharesIncludingUnicodeName) {
  Disconnect();
  ASSERT_NO_FATAL_FAILURE(Connect("żółw", true));
  Write("file", "data");
  std::array<sdlrdp_drive, 2> drives   { };
  auto                        deadline = Headless::Clock::now() + 2s;
  while (sdlrdp_drive_list(handle.Handle(), drives.data(), 2) != 2 && Headless::Clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  ASSERT_EQ(sdlrdp_drive_list(handle.Handle(), drives.data(), 2), 2);
  EXPECT_EQ((std::set<std::string>{ drives[0].name, drives[1].name }), (std::set<std::string>{ "żółw", "second" }));
  EXPECT_NE(drives[0].id, drives[1].id);
  for (auto const& entry : drives) {
    ASSERT_NO_FATAL_FAILURE(ThenSharedFile(entry));
  }
}
TEST_F(Drive, TwoHundredEntriesInPagesOfThirtyTwo) {
  ASSERT_EQ(sdlrdp_drive_mkdir(handle.Handle(), drive, "many"), 0);
  auto                          expected = GivenDirectoryEntries();
  std::set<std::string>         actual;
  std::array<sdlrdp_dirent, 32> entries  { };
  unsigned                      offset   = 0;
  ASSERT_NO_FATAL_FAILURE(WhenDirectoryPaged(entries, actual, offset));
  EXPECT_EQ(offset, 200u);
  EXPECT_EQ(actual, expected);
}
}
}
