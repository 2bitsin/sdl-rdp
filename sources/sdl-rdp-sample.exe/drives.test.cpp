#include <cstddef>
#include "_detail/sample-fixture.hpp"
#include <sdl-rdp-backend.so/_detail/headless-drive.hpp>
#include <sdl-rdp-backend.so/_detail/test-io.hpp>
#include <oxbox/platform/file-writer.hpp>
#include <openssl/evp.h>
#include <format>

namespace SampleGate {
TEST_F(Sample, DriveCommands)
{
  oxbox::platform::ScratchArea const share   { "sample-drive", "sdl-rdp" };
  std::string                        original = "client disk contents\n";
  oxbox::platform::WriteBinaryFile(share.Path() / "source", std::as_bytes(std::span(original)));
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--ls", "share", "--cat", "share/source", "--write", "share/output" });
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  Headless::ShareDrive(client, share.Path().c_str());
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  std::jthread pump([&](const std::stop_token& quit) { while (!quit.stop_requested() && client.Pump()) {} });
  ASSERT_TRUE(Read("entry name=source size=21 dir=0")) << process->transcript;
  ASSERT_TRUE(Read("ls done")) << process->transcript;
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned length = 0;
  ASSERT_EQ(EVP_Digest(original.data(), original.size(), digest, &length, EVP_sha256(), nullptr), 1);
  std::string hex;
  for (unsigned i = 0; i < length; ++i) hex += std::format("{:02x}", digest[i]);
  ASSERT_TRUE(Read("cat bytes=21 sha256=" + hex)) << process->transcript;
  ASSERT_TRUE(Read("write done")) << process->transcript;
  auto output = Headless::ReadText((share.Path() / "output").c_str());
  ASSERT_EQ(output.size(), 3 * 1024uz * 1024u);
  for (size_t i = 0; i < output.size(); ++i) {
    auto expected = i >= 1024uz * 1024 && i < static_cast<std::ptrdiff_t>(2 * 1024) * 1024 ? 0 : (i % (1024uz * 1024)) % 251;
    ASSERT_EQ(static_cast<unsigned char>(output[i]), expected) << i;
  }
  SDL_Log("%s", process->transcript.c_str());
  pump.request_stop();
  pump.join();
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}
namespace {
void VerifyStorage()
{
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_STORAGE_TITLE_DRIVER, "rdp"));
  auto* storage = SDL_OpenTitleStorage("share", 0);
  ASSERT_NE(storage, nullptr) << SDL_GetError();
  ASSERT_TRUE(SDL_StorageReady(storage));
  ASSERT_TRUE(SDL_WriteStorageFile(storage, "whole", "contents", 8)) << SDL_GetError();
  char buffer[8]{};
  ASSERT_TRUE(SDL_ReadStorageFile(storage, "whole", buffer, sizeof(buffer)));
  EXPECT_EQ(std::string(buffer, 8), "contents");
  unsigned entries = 0;
  EXPECT_TRUE(SDL_EnumerateStorageDirectory(storage, "", [](void* data, char const*, char const* name) {
    if (std::string_view(name) == "whole") ++*static_cast<unsigned*>(data);
    return SDL_ENUM_CONTINUE; }, &entries));
  EXPECT_EQ(entries, 1u);
  EXPECT_TRUE(SDL_CloseStorage(storage));
}
void VerifyStream(SDL_PropertiesID properties, fs::path const& path)
{
  char buffer[8]{};
  using Open = SDL_IOStream*(SDLCALL*)(char const*, char const*, char const*);
  auto open = reinterpret_cast<Open>(SDL_GetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_OPEN_FILE_POINTER, nullptr));
  ASSERT_NE(open, nullptr);
  auto* stream = open("share", "whole", "r+b");
  ASSERT_NE(stream, nullptr) << SDL_GetError();
  EXPECT_EQ(SDL_GetIOSize(stream), 8);
  EXPECT_EQ(SDL_SeekIO(stream, 2, SDL_IO_SEEK_SET), 2);
  EXPECT_EQ(SDL_ReadIO(stream, buffer, 3), 3u);
  EXPECT_EQ(std::string(buffer, 3), "nte");
  EXPECT_EQ(SDL_SeekIO(stream, -1, SDL_IO_SEEK_END), 7);
  EXPECT_EQ(SDL_WriteIO(stream, "!", 1), 1u);
  EXPECT_EQ(SDL_GetIOSize(stream), 8);
  EXPECT_TRUE(SDL_FlushIO(stream)) << SDL_GetError();
  EXPECT_TRUE(SDL_CloseIO(stream));
  EXPECT_EQ(Headless::ReadText((path / "whole").c_str()), "content!");
}
}
TEST_F(Sample, DriveStorageAndStream)
{
  auto backend = BuildRoot() / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so";
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_BACKEND, backend.c_str()));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_CERT_DIR, certificates.Path().c_str()));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_PORT, "0"));
  ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
  auto quit       = std::unique_ptr<void, void (*)(void*)>(reinterpret_cast<void*>(1), [](void*) { SDL_Quit(); });
  auto properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  Client                             client(SDL_GetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0), false);
  oxbox::platform::ScratchArea const share{ "storage-drive", "sdl-rdp" };
  Headless::ShareDrive(client, share.Path().c_str());
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  std::jthread const pump([&](const std::stop_token& stop) { while (!stop.stop_requested() && client.Pump()) {} });
  auto deadline = Clock::now() + 3s;
  while (!*SDL_GetStringProperty(properties, SDL_PROP_DISPLAY_RDP_DRIVES_STRING, "") && Clock::now() < deadline) {
    SDL_PumpEvents();
    SDL_Delay(1);
  }
  ASSERT_STREQ(SDL_GetStringProperty(properties, SDL_PROP_DISPLAY_RDP_DRIVES_STRING, ""), "share");
  VerifyStorage();
  VerifyStream(properties, share.Path());
}
}
