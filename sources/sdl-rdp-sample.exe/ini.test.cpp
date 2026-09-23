#include "_detail/sample-fixture.hpp"
#include <netinet/in.h>
#include <sys/socket.h>

namespace SampleGate {
namespace {
unsigned AvailablePort() {
  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  Expects(socket_fd >= 0, "port reservation socket created");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  Expects(bind(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "ephemeral port bound");
  socklen_t size = sizeof(address);
  Expects(getsockname(socket_fd, reinterpret_cast<sockaddr*>(&address), &size) == 0, "port obtained");
  close(socket_fd);
  return ntohs(address.sin_port);
}
std::vector<std::string> IniArguments(fs::path const& directory, fs::path const& certificates) {
  auto args = Arguments(certificates, false);
  std::erase_if(args, [](auto const& arg) {
    return arg.starts_with("SDL_RDP_PORT=") || arg.starts_with("SDL_RDP_BACKEND=");
  });
  args.insert(args.begin() + 1, {"-u", "SDL_RDP_PORT", "-u", "SDL_RDP_BACKEND", "-u", "SDL_RDP_INI",
                               "-u", "SDL_RDP_ASPECT", "-C", directory.string()});
  return args;
}
void WriteIni(fs::path const& file, unsigned port) {
  std::ofstream out(file);
  out << "[server]\nSDL_RDP_PORT = " << port << "\nSDL_RDP_BACKEND = \""
      << (BuildRoot() / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so").string()
      << "\"\nSDL_RDP_ASPECT = 4:3\n";
  Expects(bool(out), "ini written");
}
}
class IniSample : public Sample, public testing::WithParamInterface<bool> {};
TEST_P(IniSample, WorkingDirectoryPortAndBackend) {
  oxbox::platform::ScratchArea directory{"ini", "sdl-rdp"};
  auto port = AvailablePort();
  WriteIni(directory.Path() / "libSDL3.ini", port);
  auto args = IniArguments(directory.Path(), certificates.Path());
  if (GetParam()) args.insert(args.end() - 1, {"SDL_RDP_PORT=1", "SDL_RDP_BACKEND=/missing/backend"});
  process = std::make_unique<Process>(args);
  ASSERT_TRUE(Read("port ")) << process->transcript;
  EXPECT_EQ(Number(std::string_view(line).substr(5)), port);
  Client client(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); }));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}
INSTANTIATE_TEST_SUITE_P(IniPrecedence, IniSample, testing::Bool());

TEST_F(Sample, IniExplicitPathWinsAsWholeFile) {
  oxbox::platform::ScratchArea directory{"ini-explicit", "sdl-rdp"};
  auto port = AvailablePort();
  WriteIni(directory.Path() / "chosen.ini", port);
  { std::ofstream out(directory.Path() / "libSDL3.ini"); out << "SDL_RDP_PORT=1\nSDL_RDP_AUTH=invalid\n"; }
  auto args = IniArguments(directory.Path(), certificates.Path());
  args.insert(args.end() - 1, "SDL_RDP_INI=chosen.ini");
  process = std::make_unique<Process>(args);
  ASSERT_TRUE(Read("port ")) << process->transcript;
  EXPECT_EQ(Number(std::string_view(line).substr(5)), port);
  Client client(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, IniUnreadableExplicitPathFailsStartup) {
  oxbox::platform::ScratchArea directory{"ini-missing", "sdl-rdp"};
  WriteIni(directory.Path() / "libSDL3.ini", 0);
  auto args = IniArguments(directory.Path(), certificates.Path());
  args.insert(args.end() - 1, "SDL_RDP_INI=missing.ini");
  process = std::make_unique<Process>(args);
  ASSERT_TRUE(Read("ERROR: Could not read RDP settings file missing.ini")) << process->transcript;
  EXPECT_TRUE(process->transcript.contains("Could not read RDP settings file missing.ini"));
  EXPECT_FALSE(process->transcript.contains("port "));
}
}

namespace SampleGate {
TEST_F(Sample, IniApplicationHintWins) {
  oxbox::platform::ScratchArea directory{"ini-hint", "sdl-rdp"};
  WriteIni(directory.Path() / "libSDL3.ini", 0);
  auto args = IniArguments(directory.Path(), certificates.Path());
  args.insert(args.end(), {"--aspect", "2:1"});
  process = std::make_unique<Process>(args);
  ASSERT_TRUE(Read("port ")) << process->transcript;
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(client.Until([&] { return client.instance->context->gdi->width == 960; }));
  EXPECT_EQ(client.instance->context->gdi->height, 480);
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, IniLibraryDirectoryWinsOverWorkingDirectory) {
  oxbox::platform::ScratchArea directory{"ini-library", "sdl-rdp"};
  auto library = directory.Path() / "library";
  fs::create_directory(library);
  fs::create_symlink(BuildRoot() / "sources/SDL3.so/libSDL3.so.0", library / "libSDL3.so.0");
  auto port = AvailablePort();
  WriteIni(library / "libSDL3.ini", port);
  { std::ofstream out(directory.Path() / "libSDL3.ini"); out << "SDL_RDP_PORT=1\nSDL_RDP_AUTH=invalid\n"; }
  auto args = IniArguments(directory.Path(), certificates.Path());
  args.insert(args.end() - 1, "LD_LIBRARY_PATH=" + library.string());
  process = std::make_unique<Process>(args);
  ASSERT_TRUE(Read("port ")) << process->transcript;
  EXPECT_EQ(Number(std::string_view(line).substr(5)), port);
  Client client(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}
}

namespace SampleGate {
TEST_F(Sample, IniCodeHintsCacheAndLiveReset) {
  oxbox::platform::ScratchArea directory{"ini-cache", "sdl-rdp"};
  auto file = directory.Path() / "libSDL3.ini";
  WriteIni(file, 1);
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_INI, file.c_str()));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_CERT_DIR, certificates.Path().c_str()));
  ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_RDP_PORT, "0", SDL_HINT_OVERRIDE));
  ASSERT_TRUE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), SDL_HINT_RDP_PORT, "2", true));
  ASSERT_TRUE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), SDL_HINT_RDP_INI, "/missing/ini", true));
  ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
  auto display = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  EXPECT_GT(SDL_GetNumberProperty(display, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0), 2);
  auto window = SDL_CreateWindow("ini", 640, 480, 0);
  ASSERT_NE(window, nullptr) << SDL_GetError();
  auto properties = SDL_GetWindowProperties(window);
  EXPECT_STREQ(SDL_GetStringProperty(properties, SDL_PROP_WINDOW_RDP_ASPECT_STRING, ""), "4:3");
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_ASPECT, "2:1"));
  EXPECT_STREQ(SDL_GetStringProperty(properties, SDL_PROP_WINDOW_RDP_ASPECT_STRING, ""), "2:1");
  ASSERT_TRUE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), SDL_HINT_RDP_ASPECT, "3:1", true));
  ASSERT_TRUE(SDL_ResetHint(SDL_HINT_RDP_ASPECT));
  EXPECT_STREQ(SDL_GetStringProperty(properties, SDL_PROP_WINDOW_RDP_ASPECT_STRING, ""), "4:3");
  SDL_DestroyWindow(window);
  SDL_Quit();
  { std::ofstream out(file); out << "SDL_RDP_BACKEND=/missing/backend\nSDL_RDP_ASPECT=invalid\n"; }
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_RDP_PORT, "0", SDL_HINT_OVERRIDE));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_CERT_DIR, certificates.Path().c_str()));
  ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
  window = SDL_CreateWindow("cached ini", 640, 480, 0);
  ASSERT_NE(window, nullptr);
  EXPECT_STREQ(SDL_GetStringProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_RDP_ASPECT_STRING, ""), "4:3");
  SDL_DestroyWindow(window);
  SDL_Quit();
}
}
