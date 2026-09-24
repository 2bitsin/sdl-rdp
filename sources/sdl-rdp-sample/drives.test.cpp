#include <sdl-rdp/sample-gate.test/client-steps.hpp>
#include <sdl-rdp/sample-gate.test/sample-launch.hpp>
#include <sdl-rdp/sample-gate.test/sample.hpp>

#include <SDL3/SDL.h>
#include <openssl/evp.h>
#include <oxbox/platform/file-writer.hpp>
#include <sdl-rdp/headless-client.test/client.hpp>
#include <sdl-rdp/headless-client.test/io.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <thread>

namespace SampleGate {
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
auto ThenStorageEntries(SDL_Storage* storage) -> void {
  std::size_t entries = 0;
  EXPECT_TRUE(SDL_EnumerateStorageDirectory(
      storage, "",
      [](void* data, char const*, char const* name) {
        if (std::string_view(name) == "whole") ++*static_cast<std::uint32_t*>(data);
        return SDL_ENUM_CONTINUE;
      },
      &entries));
  EXPECT_EQ(entries, 1u);
}
auto ThenStorageContents(SDL_Storage* storage) -> void {
  ASSERT_TRUE(SDL_WriteStorageFile(storage, "whole", "contents", 8)) << SDL_GetError();
  std::array<char, 8> buffer{ };
  ASSERT_TRUE(SDL_ReadStorageFile(storage, "whole", buffer.data(), sizeof(buffer)));
  EXPECT_EQ(std::string(buffer.data(), 8), "contents");
}
auto VerifyStorage() -> void {
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_STORAGE_TITLE_DRIVER, "rdp"));
  auto* storage = SDL_OpenTitleStorage("share", 0);
  ASSERT_NE(storage, nullptr) << SDL_GetError();
  ASSERT_TRUE(SDL_StorageReady(storage));
  ASSERT_NO_FATAL_FAILURE(ThenStorageContents(storage));
  ASSERT_NO_FATAL_FAILURE(ThenStorageEntries(storage));
  EXPECT_TRUE(SDL_CloseStorage(storage));
}
auto ThenStreamRead(SDL_IOStream* stream) -> void {
  std::array<char, 8> buffer{ };
  EXPECT_EQ(SDL_GetIOSize(stream), 8);
  EXPECT_EQ(SDL_SeekIO(stream, 2, SDL_IO_SEEK_SET), 2);
  EXPECT_EQ(SDL_ReadIO(stream, buffer.data(), 3), 3u);
  EXPECT_EQ(std::string(buffer.data(), 3), "nte");
}
auto ThenStreamWrite(SDL_IOStream* stream) -> void {
  EXPECT_EQ(SDL_SeekIO(stream, -1, SDL_IO_SEEK_END), 7);
  EXPECT_EQ(SDL_WriteIO(stream, "!", 1), 1u);
  EXPECT_EQ(SDL_GetIOSize(stream), 8);
  EXPECT_TRUE(SDL_FlushIO(stream)) << SDL_GetError();
}
using OpenFile = SDL_IOStream*(SDLCALL*)(char const*, char const*, char const*);
using Stream   = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;
auto ThenAppendExtends(OpenFile open) -> void {
  Stream const append{ open(nullptr, "whole", "a+b"), SDL_CloseIO };
  ASSERT_TRUE(append) << SDL_GetError();
  auto const before = SDL_GetIOSize(append.get());
  EXPECT_EQ(SDL_SeekIO(append.get(), 0, SDL_IO_SEEK_SET), 0);
  EXPECT_EQ(SDL_WriteIO(append.get(), "+", 1), 1u);
  EXPECT_EQ(SDL_GetIOSize(append.get()), before + 1);
}
auto ThenEmptyNameSelectsFirstDrive(OpenFile open) -> void {
  EXPECT_TRUE(Stream(open("", "whole", "rb"), SDL_CloseIO)) << SDL_GetError();
  EXPECT_FALSE(Stream(open("missing-drive", "whole", "rb"), SDL_CloseIO));
}
auto VerifyAppendAndDefaultDrive(SDL_PropertiesID properties) -> void {
  auto const open = reinterpret_cast<OpenFile>(
      SDL_GetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_OPEN_FILE_POINTER, nullptr));
  ASSERT_NE(open, nullptr);
  ASSERT_NO_FATAL_FAILURE(ThenAppendExtends(open));
  ThenEmptyNameSelectsFirstDrive(open);
}
auto VerifyStream(SDL_PropertiesID properties, fs::path const& path) -> void {
  using Open = SDL_IOStream*(SDLCALL*)(char const*, char const*, char const*);
  auto open = reinterpret_cast<Open>(
      SDL_GetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_OPEN_FILE_POINTER, nullptr));
  ASSERT_NE(open, nullptr);
  auto* stream = open("share", "whole", "r+b");
  ASSERT_NE(stream, nullptr) << SDL_GetError();
  ASSERT_NO_FATAL_FAILURE(ThenStreamRead(stream));
  ASSERT_NO_FATAL_FAILURE(ThenStreamWrite(stream));
  EXPECT_TRUE(SDL_CloseIO(stream));
  EXPECT_EQ(Headless::ReadText((path / "whole").c_str()), "content!");
}
auto InitializeRdpVideo(fs::path const& certificates) -> void {
  auto backend = BackendLibrary();
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_BACKEND, backend.c_str()));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_CERT_DIR, certificates.c_str()));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_PORT, "0"));
  ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
}
}
TEST_F(Sample, DriveStorageAndStream) {
  ASSERT_NO_FATAL_FAILURE(InitializeRdpVideo(certificates.Path()));
  auto quit = std::unique_ptr<void, auto (*)(void*)->void>(reinterpret_cast<void*>(1), [](void*) { SDL_Quit(); });
  auto                               properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  Client                             client(PrimaryDisplayPort(), false);
  oxbox::platform::ScratchArea const share      { "storage-drive", "sdl-rdp" };
  ASSERT_NO_FATAL_FAILURE(ConnectDrive(client, share.Path()));
  auto const pump = PumpInBackground(client);
  ASSERT_NO_FATAL_FAILURE(ThenDriveStorage(properties));
  ASSERT_NO_FATAL_FAILURE(VerifyStorage());
  ASSERT_NO_FATAL_FAILURE(VerifyStream(properties, share.Path()));
  VerifyAppendAndDefaultDrive(properties);
}
}
