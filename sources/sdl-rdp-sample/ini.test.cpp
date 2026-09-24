#include <sdl-rdp/sample-gate.test/frame-pattern.hpp>
#include <sdl-rdp/sample-gate.test/initialized-sdl.hpp>
#include <sdl-rdp/sample-gate.test/process.hpp>
#include <sdl-rdp/sample-gate.test/sample-launch.hpp>
#include <sdl-rdp/sample-gate.test/sample.hpp>
#include <sdl-rdp/utilities/scoped.hpp>

#include <SDL3/SDL.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <utility>
#include <vector>

namespace SampleGate {
namespace {
auto ThenAspectReset(SDL_PropertiesID properties) -> void {
  ASSERT_TRUE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), SDL_HINT_RDP_ASPECT, "3:1", true));
  ASSERT_TRUE(SDL_ResetHint(SDL_HINT_RDP_ASPECT));
  EXPECT_STREQ(SDL_GetStringProperty(properties, SDL_PROP_WINDOW_RDP_ASPECT_STRING, ""), "4:3");
}
auto AvailablePort() -> std::uint32_t {
  int const socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  Expects(socket_fd >= 0, "port reservation socket created");
  sockaddr_in address{ };
  address.sin_family      = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  Expects(bind(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "ephemeral port bound");
  socklen_t size = sizeof(address);
  Expects(getsockname(socket_fd, reinterpret_cast<sockaddr*>(&address), &size) == 0, "port obtained");
  close(socket_fd);
  return ntohs(address.sin_port);
}
auto IniArguments(fs::path const& directory, fs::path const& certificates, Words const& environment = { },
                  Words const& options = { }) -> Words {
  auto args = Arguments(certificates, environment, options);
  std::erase_if(
      args, [](auto const& arg) { return arg.starts_with("SDL_RDP_PORT=") || arg.starts_with("SDL_RDP_BACKEND="); });
  args.insert(args.begin() + 1, { "-u", "SDL_RDP_PORT", "-u", "SDL_RDP_BACKEND", "-u", "SDL_RDP_INI", "-u",
                                  "SDL_RDP_ASPECT", "-C", directory.string() });
  return args;
}
auto WriteInvalidIni(fs::path const& directory) -> void {
  std::ofstream out(directory / "libSDL3.ini");
  out << "SDL_RDP_PORT=1\nSDL_RDP_AUTH=invalid\n";
}
auto WriteIni(fs::path const& file, std::uint32_t port) -> void {
  std::ofstream out(file);
  out << "[server]\nSDL_RDP_PORT = " << port << "\nSDL_RDP_BACKEND = \"" << BackendLibrary().string()
      << "\"\nSDL_RDP_ASPECT = 4:3\n";
  Expects(bool(out), "ini written");
}
}
class IniSample : public Sample, public testing::WithParamInterface<bool> { };
namespace {
auto ThenLiveAspect(SDL_Window* window) -> void {
  auto properties = SDL_GetWindowProperties(window);
  EXPECT_STREQ(SDL_GetStringProperty(properties, SDL_PROP_WINDOW_RDP_ASPECT_STRING, ""), "4:3");
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_ASPECT, "2:1"));
  EXPECT_STREQ(SDL_GetStringProperty(properties, SDL_PROP_WINDOW_RDP_ASPECT_STRING, ""), "2:1");
  ThenAspectReset(properties);
}
}
TEST_P(IniSample, WorkingDirectoryPortAndBackend) {
  oxbox::platform::ScratchArea const directory { "ini", "sdl-rdp" };
  auto                               port      = AvailablePort();
  WriteIni(directory.Path() / "libSDL3.ini", port);
  auto const overridden = GetParam() ? Words{ "SDL_RDP_PORT=1", "SDL_RDP_BACKEND=/missing/backend" } : Words{ };
  ThenIniConnects(IniArguments(directory.Path(), certificates.Path(), overridden), port);
}
INSTANTIATE_TEST_SUITE_P(IniPrecedence, IniSample, testing::Bool());

TEST_F(Sample, IniExplicitPathWinsAsWholeFile) {
  oxbox::platform::ScratchArea const directory { "ini-explicit", "sdl-rdp" };
  auto                               port      = AvailablePort();
  WriteIni(directory.Path() / "chosen.ini", port);
  WriteInvalidIni(directory.Path());
  ThenIniConnects(IniArguments(directory.Path(), certificates.Path(), { "SDL_RDP_INI=chosen.ini" }), port);
}

TEST_F(Sample, IniUnreadableExplicitPathFailsStartup) {
  oxbox::platform::ScratchArea const directory{ "ini-missing", "sdl-rdp" };
  WriteIni(directory.Path() / "libSDL3.ini", 0);
  process = std::make_unique<Process>(
      IniArguments(directory.Path(), certificates.Path(), { "SDL_RDP_INI=missing.ini" }));
  ASSERT_TRUE(Read("ERROR: Could not read RDP settings file missing.ini")) << process->Transcript();
  EXPECT_TRUE(process->Transcript().contains("Could not read RDP settings file missing.ini"));
  EXPECT_FALSE(process->Transcript().contains("port "));
}
}

namespace SampleGate {
TEST_F(Sample, IniApplicationHintWins) {
  oxbox::platform::ScratchArea const directory{ "ini-hint", "sdl-rdp" };
  WriteIni(directory.Path() / "libSDL3.ini", 0);
  ASSERT_NO_FATAL_FAILURE(Launch(IniArguments(directory.Path(), certificates.Path(), { }, { "--aspect", "2:1" })));
  auto client = AnnouncedClient(640, 480);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(client.Until([&] { return client.Instance()->context->gdi->width == 960; }));
  EXPECT_EQ(client.Instance()->context->gdi->height, 480);
  Escape(client);
}

TEST_F(Sample, IniLibraryDirectoryWinsOverWorkingDirectory) {
  oxbox::platform::ScratchArea const directory { "ini-library", "sdl-rdp" };
  auto                               library   = directory.Path() / "library";
  fs::create_directory(library);
  fs::create_symlink(BuildRoot() / "sources/SDL3/libSDL3.so.0", library / "libSDL3.so.0");
  auto port = AvailablePort();
  WriteIni(library / "libSDL3.ini", port);
  WriteInvalidIni(directory.Path());
  ThenIniConnects(IniArguments(directory.Path(), certificates.Path(), { "LD_LIBRARY_PATH=" + library.string() }), port);
}
}

namespace SampleGate {
TEST_F(Sample, IniCodeHintsCacheAndLiveReset) {
  oxbox::platform::ScratchArea const directory { "ini-cache", "sdl-rdp" };
  auto                               file      = directory.Path() / "libSDL3.ini";
  WriteIni(file, 1);
  ASSERT_NO_FATAL_FAILURE(GivenIniHints(file));
  EXPECT_GT(PrimaryDisplayPort(), 2u);
  auto* window = SDL_CreateWindow("ini", 640, 480, 0);
  ASSERT_NE(window, nullptr) << SDL_GetError();
  ASSERT_NO_FATAL_FAILURE(ThenLiveAspect(window));
  SDL_DestroyWindow(window);
  SDL_Quit();
  WriteIni(file, 1);
  {
    std::ofstream out(file, std::ios::app);
    out << "SDL_RDP_ASPECT=2:1\n";
  }
  ThenReloadedIni(file);
}
}

namespace SampleGate {
namespace {
using Storage = std::unique_ptr<SDL_Storage, decltype(&SDL_CloseStorage)>;
class IniFile {
public:
  explicit IniFile(char const* name) : _directory{ name, "sdl-rdp" }, _path{ _directory.Path() / "libSDL3.ini" } {
    WriteIni(_path, 1);
  }
  auto Path() const -> fs::path const& {
    return _path;
  }
private:
  oxbox::platform::ScratchArea const _directory;
  fs::path const                     _path;
};
auto RdpTitleStorage() -> Storage {
  EXPECT_TRUE(SDL_SetHint(SDL_HINT_STORAGE_TITLE_DRIVER, "rdp"));
  return { SDL_OpenTitleStorage("", 0), SDL_CloseStorage };
}
auto WindowAspect(Window const& window) -> std::string {
  return SDL_GetStringProperty(SDL_GetWindowProperties(window.get()), SDL_PROP_WINDOW_RDP_ASPECT_STRING, "");
}
auto ThenVideoRejoinsDriver(fs::path const& ini) -> void {
  auto const port = PrimaryDisplayPort();
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  std::ofstream{ ini, std::ios::app } << "SDL_RDP_ASPECT=2:1\n";
  ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO)) << SDL_GetError();
  EXPECT_EQ(PrimaryDisplayPort(), port);
}
auto ThenWindowAspect(char const* expected) -> void {
  Window const window{ SDL_CreateWindow("aspect", 640, 480, 0), SDL_DestroyWindow };
  ASSERT_TRUE(window) << SDL_GetError();
  EXPECT_EQ(WindowAspect(window), expected);
}
auto ThenInvalidAspectFailsWindow(char const* aspect) -> void {
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_ASPECT, aspect));
  EXPECT_EQ(Window(SDL_CreateWindow("invalid aspect", 640, 480, 0), SDL_DestroyWindow), nullptr);
  EXPECT_TRUE(std::string_view(SDL_GetError()).contains("aspect")) << SDL_GetError();
}
constexpr auto StorageSpaceChild = "SDL_RDP_TEST_STORAGE_SPACE_CHILD";
auto CurrentTest() -> std::string {
  auto const& test = *testing::UnitTest::GetInstance()->current_test_info();
  return std::string(test.test_suite_name()) + "." + test.name();
}
auto RerunInChild() -> Process {
  return Process{ { "env", std::string(StorageSpaceChild) + "=1", fs::read_symlink("/proc/self/exe").string(),
                    "--gtest_filter=" + CurrentTest() } };
}
auto ThenChildStops(Process& child) -> void {
  std::string line;
  while (child.Line(line, Clock::now() + 10s)) {
  }
  EXPECT_FALSE(child.Exit());
  EXPECT_TRUE(child.Transcript().contains("free-space query")) << child.Transcript();
}
auto ThenStorageSpaceStops(Storage const& storage) -> void {
  if (std::getenv(StorageSpaceChild) != nullptr) {
    std::ignore = SDL_GetStorageSpaceRemaining(storage.get());
    return;
  }
  auto child = RerunInChild();
  ThenChildStops(child);
}
auto ThenStorageSpaceIsNotImplemented(Storage const& storage) -> void {
  if constexpr (utilities::detail::contract::Mode() == oxbox::platform::ContractMode::STOP)
    ThenStorageSpaceStops(storage);
  else
    EXPECT_EQ(SDL_GetStorageSpaceRemaining(storage.get()), 0);
}
auto WhenInitializedWith(std::pair<char const*, char const*> const& setting) -> void {
  EXPECT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
  EXPECT_TRUE(SDL_SetHint(SDL_HINT_RDP_BACKEND, "/missing/backend"));
  EXPECT_TRUE(SDL_SetHint(setting.first, setting.second));
  EXPECT_FALSE(SDL_Init(SDL_INIT_VIDEO));
}
}
class IniSession : public Sample {
protected:
  auto SetUp() -> void override {
    ASSERT_NO_FATAL_FAILURE(Sample::SetUp());
    _sdl.emplace([this] {
      GivenIniHints(_ini.Path());
      return true;
    });
  }
  auto IniPath() const -> fs::path const& {
    return _ini.Path();
  }
private:
  IniFile const                 _ini{ testing::UnitTest::GetInstance()->current_test_info()->name() };
  std::optional<InitializedSdl> _sdl;
};
TEST_F(IniSession, PartialVideoQuitKeepsDriverAndIniSnapshot) {
  Storage const storage = RdpTitleStorage();
  ASSERT_TRUE(storage) << SDL_GetError();
  ASSERT_NO_FATAL_FAILURE(ThenVideoRejoinsDriver(IniPath()));
  ThenWindowAspect("4:3");
}
TEST_F(IniSession, InvalidAspectHintFailsWindowCreationAndCanRecover) {
  for (auto const* aspect : { "1:0", "4:3:2", "4:x", "4 : 3" }) ThenInvalidAspectFailsWindow(aspect);
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_ASPECT, "4:3"));
  ThenWindowAspect("4:3");
}
TEST_F(IniSession, StorageSpaceDiagnosesUnsupportedBackendOperation) {
  Storage const storage = RdpTitleStorage();
  ASSERT_TRUE(storage) << SDL_GetError();
  ThenStorageSpaceIsNotImplemented(storage);
}
class InvalidInteger : public Sample, public testing::WithParamInterface<std::pair<char const*, char const*>> { };
TEST_P(InvalidInteger, FailsBeforeBackendLoadingAndNamesTheSetting) {
  InitializedSdl const sdl{ [&] {
    WhenInitializedWith(GetParam());
    return true;
  } };
  EXPECT_TRUE(std::string_view(SDL_GetError()).contains(GetParam().first)) << SDL_GetError();
}
INSTANTIATE_TEST_SUITE_P(Settings, InvalidInteger,
                         testing::Values(std::pair{ SDL_HINT_RDP_PORT, "-5" }, std::pair{ SDL_HINT_RDP_WIDTH, "+640" },
                                         std::pair{ SDL_HINT_RDP_WIDTH, "-1" }, std::pair{ SDL_HINT_RDP_PORT, "3389x" },
                                         std::pair{ SDL_HINT_RDP_WIDTH, "640 480" }));
}
