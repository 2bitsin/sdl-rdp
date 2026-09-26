#include <sdl-rdp/sample-gate.test/client/steps.hpp>
#include <sdl-rdp/sample-gate.test/process/initialized-sdl.hpp>
#include <sdl-rdp/sample-gate.test/sample/launch.hpp>
#include <sdl-rdp/sample-gate.test/sample/sample.hpp>

#include <SDL3/SDL.h>
#include <openssl/evp.h>
#include <oxbox/platform/file-writer.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/utilities/io.hpp>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>

namespace sdl_rdp::integration::sample_test::detail::drives {
using namespace std::chrono_literals;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Clock;
using sdl_rdp::headless_client_test::utilities::ReadText;
using sdl_rdp::sample_gate_test::client::ConnectDrive;
using sdl_rdp::sample_gate_test::process::InitializedSdl;
using sdl_rdp::sample_gate_test::process::Storage;
using sdl_rdp::sample_gate_test::sample::PrimaryDisplayPort;
using sdl_rdp::sample_gate_test::sample::Sample;
using sdl_rdp::sample_gate_test::sample::SetLoopbackHints;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Releases;

namespace {
auto ThenDriveStorage(SDL_PropertiesID properties) -> void {
  auto deadline = Clock::now() + 3s;
  while (!*SDL_GetStringProperty(properties, SDL_PROP_DISPLAY_RDP_DRIVES_STRING, "") && Clock::now() < deadline) {
    SDL_PumpEvents();
    SDL_Delay(1);
  }
  ASSERT_STREQ(SDL_GetStringProperty(properties, SDL_PROP_DISPLAY_RDP_DRIVES_STRING, ""), "share");
}
}
TEST_F(Sample, DriveCommands) {
  oxbox::platform::ScratchArea const share    { "sample-drive", "sdl-rdp" };
  std::string                        original = "client disk contents\n";
  oxbox::platform::WriteBinaryFile(share.Path() / "source", std::as_bytes(std::span(original)));
  ASSERT_NO_FATAL_FAILURE(
      GivenDriveProcess({ "--ls", "share", "--cat", "share/source", "--write", "share/output" }, share.Path()));
  auto& client = SessionClient();
  auto  pump   = PumpInBackground(client);
  ASSERT_TRUE(Read("entry name=source size=21 dir=0")) << process->Transcript();
  ASSERT_TRUE(Read("ls done")) << process->Transcript();
  ASSERT_NO_FATAL_FAILURE(ThenDriveOutput(share.Path(), original));
  SDL_Log("%s", process->Transcript().c_str());
  pump.request_stop();
  pump.join();
  Escape(client);
}
namespace {
// SDL_PROP_DISPLAY_RDP_OPEN_FILE_POINTER: the driver's `SDL_IOStream* (char const*, char const*, char const*)`.
using OpenFile = auto(char const*, char const*, char const*) -> SDL_IOStream*;
using Stream   = std::unique_ptr<SDL_IOStream, Releases<SDL_CloseIO>>;
auto ThenStorageEntries(SDL_Storage& storage) -> void {
  std::size_t entries = 0;
  EXPECT_TRUE(SDL_EnumerateStorageDirectory(
      &storage, "",
      [](void* data, char const*, char const* name) {
        Expects(data != nullptr, "the enumeration carries its counter");
        Expects(name != nullptr, "the enumerated entry has a name");
        if (std::string_view(name) == "whole") ++*static_cast<std::size_t*>(data);
        return SDL_ENUM_CONTINUE;
      },
      &entries));
  EXPECT_EQ(entries, 1u);
}
auto ThenStorageContents(SDL_Storage& storage) -> void {
  ASSERT_TRUE(SDL_WriteStorageFile(&storage, "whole", "contents", 8)) << SDL_GetError();
  std::array<char, 8> buffer{ };
  ASSERT_TRUE(SDL_ReadStorageFile(&storage, "whole", buffer.data(), sizeof(buffer)));
  EXPECT_EQ(std::string(buffer.data(), 8), "contents");
}
auto ThenEmptyFileRoundTrips(SDL_Storage& storage) -> void {
  ASSERT_TRUE(SDL_WriteStorageFile(&storage, "empty", nullptr, 0)) << SDL_GetError();
  std::uint64_t length = 1;
  ASSERT_TRUE(SDL_GetStorageFileSize(&storage, "empty", &length)) << SDL_GetError();
  EXPECT_EQ(length, 0u);
  EXPECT_TRUE(SDL_ReadStorageFile(&storage, "empty", nullptr, 0)) << SDL_GetError();
}
auto ThenStorageFiles(SDL_Storage& storage) -> void {
  ASSERT_NO_FATAL_FAILURE(ThenStorageContents(storage));
  ASSERT_NO_FATAL_FAILURE(ThenEmptyFileRoundTrips(storage));
  ASSERT_NO_FATAL_FAILURE(ThenStorageEntries(storage));
}
auto VerifyStorage() -> void {
  Storage storage{ SDL_OpenTitleStorage("share", 0) };
  ASSERT_NE(storage, nullptr) << SDL_GetError();
  ASSERT_TRUE(SDL_StorageReady(storage.get()));
  ASSERT_NO_FATAL_FAILURE(ThenStorageFiles(*storage));
  EXPECT_TRUE(SDL_CloseStorage(storage.release()));
}
auto ThenStreamRead(SDL_IOStream& stream) -> void {
  std::array<char, 8> buffer{ };
  EXPECT_EQ(SDL_GetIOSize(&stream), 8);
  EXPECT_EQ(SDL_SeekIO(&stream, 2, SDL_IO_SEEK_SET), 2);
  EXPECT_EQ(SDL_ReadIO(&stream, buffer.data(), 3), 3u);
  EXPECT_EQ(std::string(buffer.data(), 3), "nte");
}
auto ThenStreamWrite(SDL_IOStream& stream) -> void {
  EXPECT_EQ(SDL_SeekIO(&stream, -1, SDL_IO_SEEK_END), 7);
  EXPECT_EQ(SDL_WriteIO(&stream, "!", 1), 1u);
  EXPECT_EQ(SDL_GetIOSize(&stream), 8);
  EXPECT_TRUE(SDL_FlushIO(&stream)) << SDL_GetError();
}
auto ThenAppendExtends(OpenFile& open) -> void {
  Stream const append{ open(nullptr, "whole", "a+b") };
  ASSERT_TRUE(append) << SDL_GetError();
  auto const before = SDL_GetIOSize(append.get());
  EXPECT_EQ(SDL_SeekIO(append.get(), 0, SDL_IO_SEEK_SET), 0);
  EXPECT_EQ(SDL_WriteIO(append.get(), "+", 1), 1u);
  EXPECT_EQ(SDL_GetIOSize(append.get()), before + 1);
}
auto ThenEmptyNameSelectsFirstDrive(OpenFile& open) -> void {
  EXPECT_TRUE(Stream{ open("", "whole", "rb") }) << SDL_GetError();
  EXPECT_FALSE(Stream{ open("missing-drive", "whole", "rb") });
}
auto WhenOpenFile(SDL_PropertiesID properties, std::invocable<OpenFile&> auto const& use) -> void {
  // C ABI: SDL hands the driver's open function back as an untyped property pointer.
  auto const open = reinterpret_cast<OpenFile*>(
      SDL_GetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_OPEN_FILE_POINTER, nullptr));
  ASSERT_NE(open, nullptr);
  use(*open);
}
auto VerifyAppendAndDefaultDrive(OpenFile& open) -> void {
  ASSERT_NO_FATAL_FAILURE(ThenAppendExtends(open));
  ThenEmptyNameSelectsFirstDrive(open);
}
auto VerifyStream(OpenFile& open, std::filesystem::path const& path) -> void {
  Stream stream{ open("share", "whole", "r+b") };
  ASSERT_NE(stream, nullptr) << SDL_GetError();
  ASSERT_NO_FATAL_FAILURE(ThenStreamRead(*stream));
  ASSERT_NO_FATAL_FAILURE(ThenStreamWrite(*stream));
  EXPECT_TRUE(SDL_CloseIO(stream.release()));
  EXPECT_EQ(ReadText(path / "whole"), "content!");
}
}
TEST_F(Sample, DriveStorageAndStream) {
  InitializedSdl const sdl{ [&] {
    return SetLoopbackHints(certificates.Path(), { { .name = SDL_HINT_STORAGE_TITLE_DRIVER, .value = "rdp" } })
           && SDL_Init(SDL_INIT_VIDEO);
  } };
  ASSERT_TRUE(sdl.Get()) << SDL_GetError();
  auto                               properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  Client                             client(PrimaryDisplayPort(), false);
  oxbox::platform::ScratchArea const share      { "storage-drive", "sdl-rdp" };
  ASSERT_NO_FATAL_FAILURE(ConnectDrive(client, share.Path()));
  auto const pump = PumpInBackground(client);
  ASSERT_NO_FATAL_FAILURE(ThenDriveStorage(properties));
  ASSERT_NO_FATAL_FAILURE(VerifyStorage());
  ASSERT_NO_FATAL_FAILURE(WhenOpenFile(properties, [&](OpenFile& open) { VerifyStream(open, share.Path()); }));
  WhenOpenFile(properties, VerifyAppendAndDefaultDrive);
}
}
