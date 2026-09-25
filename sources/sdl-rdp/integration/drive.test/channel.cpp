#include <sdl-rdp/drive/directory-entry.hpp>
#include <sdl-rdp/drive/drive.hpp>
#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/drive/file.hpp>
#include <sdl-rdp/drive/files.hpp>
#include <sdl-rdp/headless-client.test/drive/checks.hpp>
#include <sdl-rdp/headless-client.test/drive/rdpdr-packets.hpp>
#include <sdl-rdp/headless-client.test/utilities/io.hpp>
#include <sdl-rdp/headless-client.test/utilities/thrown-text.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <future>
#include <limits>
#include <string_view>
#include <utility>
namespace sdl_rdp::integration::drive_test::detail::channel {
using sdl_rdp::drive::DirectoryEntry;
using sdl_rdp::drive::DriveFiles;
using sdl_rdp::drive::File;
using sdl_rdp::drive::FileKind;
using namespace std::chrono_literals;
using sdl_rdp::drive::PeerDisconnected;
using sdl_rdp::drive::StatusFailure;
using sdl_rdp::headless_client_test::client::Clock;
using sdl_rdp::headless_client_test::drive::DriveChecks;
using sdl_rdp::headless_client_test::drive::DriveObserver;
using sdl_rdp::headless_client_test::drive::Pattern;
using sdl_rdp::headless_client_test::drive::ReadAt;
using sdl_rdp::headless_client_test::drive::ReplyTo;
using sdl_rdp::headless_client_test::drive::WriteAt;
using sdl_rdp::headless_client_test::utilities::ReadText;
using sdl_rdp::headless_client_test::utilities::ThrownText;
using sdl_rdp::utilities::InvalidArguments;

namespace {
auto ReadSharedFile(DriveFiles const& files, std::uint32_t drive, std::string const& name, std::string const& source)
    -> bool {
  auto const  file  = files.Open(drive, name, { .read = true }, FileKind::File);
  std::string bytes(source.size(), '\0');
  auto const  count = ReadAt(*file, 0, bytes);
  file->Close();
  return count == source.size() && bytes == source;
}
auto ThenReaders(std::span<std::future<bool>> readers) -> void {
  for (auto& result : readers) {
    ASSERT_EQ(result.wait_for(10s), std::future_status::ready);
    EXPECT_TRUE(result.get());
  }
}
std::array<std::pair<std::uint32_t, std::string_view>, 13> constexpr FailureStatusNames{ {
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
    { 0xdeadbeef                  , "unknown NTSTATUS (0xdeadbeef)"             },
} };
auto ThenMissingDriveFile(DriveFiles const& files, std::uint32_t drive) -> void {
  auto const failure = ThrownText<StatusFailure>(
      [&] { return files.Open(drive, "missing.img", { .read = true }, FileKind::File); });
  EXPECT_NE(failure.find("missing.img"), std::string::npos);
}
class SharedDrive : public DriveChecks {
protected:
  auto WhenDirectoryPaged(std::set<std::string>& actual, std::size_t& offset) -> void {
    for (;;) {
      auto const page = Files().Enumerate(drive, "many", offset, 32);
      ASSERT_NO_FATAL_FAILURE(ThenUniquePage(page, actual));
      offset += page.size();
      if (page.size() < 32) break;
      ASSERT_LE(offset, 200u);
    }
  }
  auto ThenRemovedEvent(std::uint32_t old) -> void {
    auto deadline = Clock::now() + 2s;
    while (!Files().List().empty() && Clock::now() < deadline) std::this_thread::sleep_for(1ms);
    EXPECT_TRUE(Files().List().empty());
    EXPECT_EQ(PolledDriveName(false, old), "share");
  }
  auto ThenFileMetadata(std::string const& source) -> void {
    auto const info = Files().Stat(drive, "disk.img");
    EXPECT_EQ(info.size, source.size());
    EXPECT_FALSE(info.directory);
    EXPECT_GT(info.modified, 0);
  }
  auto ThenNoDriveRequests(DriveObserver const& observer) -> void {
    for (std::size_t i = 0; i < 10; ++i) ASSERT_TRUE(client->Pump());
    EXPECT_EQ(observer.Observed().requests, 0u);
  }
  static auto ThenUniquePage(std::span<DirectoryEntry const> entries, std::set<std::string>& actual) -> void {
    for (auto const& entry : entries) EXPECT_TRUE(actual.insert(entry.name).second);
  }
  auto ThenSharedFile(sdl_rdp::drive::Drive const& entry) -> void {
    auto const          file  = Files().Open(entry.id, "file", { .read = true }, FileKind::File);
    std::array<char, 4> bytes { };
    EXPECT_EQ(ReadAt(*file, 0, bytes), 4u);
    EXPECT_EQ(std::string(bytes.data(), bytes.size()), "data");
    file->Close();
  }
  auto ThenReadEndsAtDisconnect(File& file) -> void {
    pump.request_stop();
    pump.join();
    auto read = std::async(std::launch::async, [&] {
      std::string bytes(static_cast<std::ptrdiff_t>(3 * 1024) * 1024, '\0');
      return ReadAt(file, 0, bytes);
    });
    std::this_thread::sleep_for(20ms);
    Disconnect();
    ASSERT_EQ(read.wait_for(2s), std::future_status::ready);
    EXPECT_THROW(std::ignore = read.get(), PeerDisconnected);
  }
  auto ThenOverflowingOffset(File& file, DriveObserver const& observer) -> void {
    std::array<char, 1> byte{ };
    EXPECT_THROW(std::ignore = ReadAt(file, std::numeric_limits<std::uint64_t>::max(), byte), InvalidArguments);
    ASSERT_NO_FATAL_FAILURE(ThenNoDriveRequests(observer));
  }
  auto ThenClientFailure(DriveObserver& observer, std::uint32_t status, std::string_view text) -> void {
    SCOPED_TRACE(text);
    observer.Observed().io.clear();
    auto open = std::async(std::launch::async, [&] {
      return ThrownText<StatusFailure>(
          [&] { return Files().Open(drive, "missing.bin", { .read = true }, FileKind::File); });
    });
    ASSERT_TRUE(client->Until([&] { return !observer.Observed().io.empty(); }));
    auto response = ReplyTo(observer.Observed().io.front(), status);
    ASSERT_TRUE(observer.Send(response));
    ASSERT_TRUE(client->Until([&] { return open.wait_for(0s) == std::future_status::ready; }));
    EXPECT_EQ(open.get(), std::format("Drive 'missing.bin' failed: {}", text));
  }
  auto ThenDirectoryOpenRejected() -> void {
    auto const failure = ThrownText<StatusFailure>(
        [&] { return Files().Open(drive, "file", { }, FileKind::Directory); });
    EXPECT_NE(failure.find("STATUS_NOT_A_DIRECTORY (0xc0000103)"), std::string::npos);
  }
  static auto ThenSparseSize(File& file, std::uint64_t offset) -> void {
    auto const info = file.Stat();
    EXPECT_EQ(info.size, offset + 4);
    file.Close();
  }
  auto WhenFileRewritten(File& file, std::string& source) -> void {
    auto pattern = Pattern(1024uz * 1024, 29);
    for (auto offset : { 0uz, 2uz * 1024 * 1024 }) {
      ASSERT_EQ(WriteAt(file, offset, pattern), pattern.size());
      source.replace(offset, pattern.size(), pattern);
    }
    file.Close();
    EXPECT_EQ(ReadText(scratch.Path() / "disk.img"), source);
  }
};
TEST_F(SharedDrive, ReadWriteMetadataAndDirectories) {
  auto source = Pattern(static_cast<std::ptrdiff_t>(3 * 1024) * 1024);
  Write("disk.img", source);
  auto const opened = Open("disk.img", { .read = true, .write = true });
  auto&      file   = *opened;
  ASSERT_NO_FATAL_FAILURE(ThenReadRanges(file, source));
  ASSERT_NO_FATAL_FAILURE(WhenFileRewritten(file, source));
  ASSERT_NO_FATAL_FAILURE(ThenFileMetadata(source));
}
TEST_F(SharedDrive, EnumerateAndMutate) {
  Files().MakeDirectory(drive, "folder");
  Write("folder/żółw.txt", "hello");
  Write("folder/second", "other");
  auto const first = Files().Enumerate(drive, "folder", 0, 1);
  ASSERT_EQ(first.size(), 1u);
  EXPECT_EQ(first[0].size, 5u);
  auto const second = Files().Enumerate(drive, "folder", 1, 1);
  ASSERT_EQ(second.size(), 1u);
  EXPECT_NE(first[0].name, second[0].name);
  EXPECT_TRUE(Files().Enumerate(drive, "folder", 2, 1).empty());
  Files().Rename(drive, "folder/żółw.txt", "folder/renamed");
  EXPECT_TRUE(std::filesystem::exists(scratch.Path() / "folder/renamed"));
  Files().Remove(drive, "folder/renamed");
  Files().Remove(drive, "folder/second");
  Files().Remove(drive, "folder");
  ThenMissingDriveFile(Files(), drive);
}
TEST_F(SharedDrive, ConcurrentReadsAndReconnect) {
  std::vector<std::future<bool>> readers;
  for (std::size_t i = 0; i < 4; ++i) {
    auto name   = std::to_string(i);
    auto source = Pattern(static_cast<std::ptrdiff_t>(3 * 1024) * 1024, i);
    Write(name, source);
    readers.push_back(
        std::async(std::launch::async, [&, name, source] { return ReadSharedFile(Files(), drive, name, source); }));
  }
  ASSERT_NO_FATAL_FAILURE(ThenReaders(readers));
  auto const opened = Open("0");
  auto       old    = drive;
  Disconnect();
  std::string bytes(1024, '\0');
  EXPECT_THROW(std::ignore = ReadAt(*opened, 0, bytes), PeerDisconnected);
  EXPECT_THROW(opened->Close(), PeerDisconnected);
  ASSERT_NO_FATAL_FAILURE(Connect());
  EXPECT_NE(drive, old);
}
TEST_F(SharedDrive, DisconnectDuringRead) {
  Write("large", Pattern(static_cast<std::ptrdiff_t>(3 * 1024) * 1024));
  auto const opened = Open("large");
  auto&      file   = *opened;
  ASSERT_NO_FATAL_FAILURE(ThenReadEndsAtDisconnect(file));
  EXPECT_THROW(file.Close(), PeerDisconnected);
  EXPECT_TRUE(Files().List().empty());
  Connect();
}
TEST_F(SharedDrive, SparseOffsetAboveFourGiB) {
  auto const              opened = Open("sparse", { .read = true, .write = true, .create = true });
  auto&                   file   = *opened;
  constexpr std::uint64_t offset = (std::uint64_t{ 1 } << 32) + 123;
  EXPECT_EQ(WriteAt(file, offset, "high"), 4u);
  std::array<char, 4> bytes{ };
  EXPECT_EQ(ReadAt(file, offset, bytes), 4u);
  EXPECT_EQ(std::string(bytes.data(), 4), "high");
  ThenSparseSize(file, offset);
}

TEST_F(SharedDrive, AnnounceAndRemoveEvents) {
  EXPECT_EQ(PolledDriveName(true, drive), "share");
  auto old = drive;
  Disconnect();
  ASSERT_NO_FATAL_FAILURE(ThenRemovedEvent(old));
  ASSERT_NO_FATAL_FAILURE(Connect());
  EXPECT_NE(drive, old);
}

TEST_F(SharedDrive, RejectsFileDirectoryMismatchWithClientStatus) {
  Write("file", "data");
  Files().MakeDirectory(drive, "directory");
  auto const failure = ThrownText<StatusFailure>(
      [&] { return Files().Open(drive, "directory", { .read = true }, FileKind::File); });
  EXPECT_NE(failure.find("STATUS_ACCESS_DENIED (0xc0000022)"), std::string::npos);
  ThenDirectoryOpenRejected();
}
TEST_F(SharedDrive, ClientFailureStatusNames) {
  ASSERT_NO_FATAL_FAILURE(HoldRequests());
  auto& observer = *this->observer;
  for (auto [status, text] : FailureStatusNames) {
    ASSERT_NO_FATAL_FAILURE(ThenClientFailure(observer, status, text));
  }
}
TEST_F(SharedDrive, OverflowingOffsetSendsNoRequest) {
  Write("file", "data");
  auto const opened = Open("file");
  auto&      file   = *opened;
  pump.request_stop();
  pump.join();
  {
    DriveObserver const observer(*client);
    ASSERT_NO_FATAL_FAILURE(ThenOverflowingOffset(file, observer));
  }
  pump = PumpInBackground(*client);
  file.Close();
}
TEST_F(SharedDrive, TwoSharesIncludingUnicodeName) {
  Disconnect();
  ASSERT_NO_FATAL_FAILURE(Connect("żółw", true));
  Write("file", "data");
  auto deadline = Clock::now() + 2s;
  while (Files().List().size() != 2 && Clock::now() < deadline) std::this_thread::sleep_for(1ms);
  auto const drives = Files().List();
  ASSERT_EQ(drives.size(), 2u);
  EXPECT_EQ((std::set<std::string>{ drives[0].name, drives[1].name }), (std::set<std::string>{ "żółw", "second" }));
  EXPECT_NE(drives[0].id, drives[1].id);
  for (auto const& entry : drives) {
    ASSERT_NO_FATAL_FAILURE(ThenSharedFile(entry));
  }
}
TEST_F(SharedDrive, TwoHundredEntriesInPagesOfThirtyTwo) {
  Files().MakeDirectory(drive, "many");
  auto                  expected = GivenDirectoryEntries();
  std::set<std::string> actual;
  std::size_t           offset   = 0;
  ASSERT_NO_FATAL_FAILURE(WhenDirectoryPaged(actual, offset));
  EXPECT_EQ(offset, 200u);
  EXPECT_EQ(actual, expected);
}
}
}
