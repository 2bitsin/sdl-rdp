#include "_detail/sample-fixture.hpp"

#include <cstddef>
#include <format>
#include <openssl/evp.h>
#include <oxbox/platform/file-writer.hpp>
#include <sdl-rdp-backend.so/_detail/headless-drive.hpp>
#include <sdl-rdp-backend.so/_detail/test-io.hpp>

namespace SampleGate {
namespace {
void ThenDriveStorage(SDL_PropertiesID properties) {
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
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--ls", "share", "--cat", "share/source", "--write", "share/output" });
  GivenDriveProcess(arguments, share.Path());
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = SessionClient();
  std::jthread pump([&](std::stop_token const& quit) {
    while (!quit.stop_requested() && client.Pump()) {
    }
  });
  ASSERT_TRUE(Read("entry name=source size=21 dir=0")) << process->Transcript();
  ASSERT_TRUE(Read("ls done")) << process->Transcript();
  ThenDriveOutput(share.Path(), original);
  if (::testing::Test::HasFatalFailure()) return;
  SDL_Log("%s", process->Transcript().c_str());
  pump.request_stop();
  pump.join();
  Escape(client);
}
namespace {
void ThenStorageEntries(SDL_Storage* storage) {
  unsigned entries = 0;
  EXPECT_TRUE(SDL_EnumerateStorageDirectory(
      storage, "",
      [](void* data, char const*, char const* name) {
        if (std::string_view(name) == "whole") ++*static_cast<unsigned*>(data);
        return SDL_ENUM_CONTINUE;
      },
      &entries));
  EXPECT_EQ(entries, 1u);
}
void ThenStorageContents(SDL_Storage* storage) {
  ASSERT_TRUE(SDL_WriteStorageFile(storage, "whole", "contents", 8)) << SDL_GetError();
  std::array<char, 8> buffer{ };
  ASSERT_TRUE(SDL_ReadStorageFile(storage, "whole", buffer.data(), sizeof(buffer)));
  EXPECT_EQ(std::string(buffer.data(), 8), "contents");
}
void VerifyStorage() {
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_STORAGE_TITLE_DRIVER, "rdp"));
  auto* storage = SDL_OpenTitleStorage("share", 0);
  ASSERT_NE(storage, nullptr) << SDL_GetError();
  ASSERT_TRUE(SDL_StorageReady(storage));
  ThenStorageContents(storage);
  if (::testing::Test::HasFatalFailure()) return;
  ThenStorageEntries(storage);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_TRUE(SDL_CloseStorage(storage));
}
void ThenStreamRead(SDL_IOStream* stream) {
  std::array<char, 8> buffer{ };
  EXPECT_EQ(SDL_GetIOSize(stream), 8);
  EXPECT_EQ(SDL_SeekIO(stream, 2, SDL_IO_SEEK_SET), 2);
  EXPECT_EQ(SDL_ReadIO(stream, buffer.data(), 3), 3u);
  EXPECT_EQ(std::string(buffer.data(), 3), "nte");
}
void ThenStreamWrite(SDL_IOStream* stream) {
  EXPECT_EQ(SDL_SeekIO(stream, -1, SDL_IO_SEEK_END), 7);
  EXPECT_EQ(SDL_WriteIO(stream, "!", 1), 1u);
  EXPECT_EQ(SDL_GetIOSize(stream), 8);
  EXPECT_TRUE(SDL_FlushIO(stream)) << SDL_GetError();
}
void VerifyStream(SDL_PropertiesID properties, fs::path const& path) {
  using Open = SDL_IOStream*(SDLCALL*)(char const*, char const*, char const*);
  auto open =
      reinterpret_cast<Open>(SDL_GetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_OPEN_FILE_POINTER, nullptr));
  ASSERT_NE(open, nullptr);
  auto* stream = open("share", "whole", "r+b");
  ASSERT_NE(stream, nullptr) << SDL_GetError();
  ThenStreamRead(stream);
  if (::testing::Test::HasFatalFailure()) return;
  ThenStreamWrite(stream);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_TRUE(SDL_CloseIO(stream));
  EXPECT_EQ(Headless::ReadText((path / "whole").c_str()), "content!");
}
}
TEST_F(Sample, DriveStorageAndStream) {
  auto backend = BuildRoot() / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so";
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_BACKEND, backend.c_str()));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_CERT_DIR, certificates.Path().c_str()));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_PORT, "0"));
  ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
  auto quit       = std::unique_ptr<void, void (*)(void*)>(reinterpret_cast<void*>(1), [](void*) { SDL_Quit(); });
  auto properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  Client client(SDL_GetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0), false);
  oxbox::platform::ScratchArea const share{ "storage-drive", "sdl-rdp" };
  ConnectDrive(client, share.Path());
  if (::testing::Test::HasFatalFailure()) return;
  std::jthread const pump([&](std::stop_token const& stop) {
    while (!stop.stop_requested() && client.Pump()) {
    }
  });
  ThenDriveStorage(properties);
  if (::testing::Test::HasFatalFailure()) return;
  VerifyStorage();
  VerifyStream(properties, share.Path());
}
}
