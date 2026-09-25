#include <sdl-rdp/settings/settings.hpp>
#include <sdl-rdp/sample-gate.test/frame/pattern.hpp>
#include <sdl-rdp/sample-gate.test/process/initialized-sdl.hpp>
#include <sdl-rdp/sample-gate.test/process/process.hpp>
#include <sdl-rdp/sample-gate.test/sample/launch.hpp>
#include <sdl-rdp/sample-gate.test/sample/sample.hpp>
#include <sdl-rdp/settings/hint.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/scoped.hpp>

#include <SDL3/SDL.h>
#include <oxbox/serialization/io.hpp>
#include <oxbox/serialization/query.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <utility>
#include <vector>

namespace sdl_rdp::integration::sample_test::detail::settings {
using namespace std::chrono_literals;
using sdl_rdp::headless_client_test::client::Clock;
using sdl_rdp::sample_gate_test::process::InitializedSdl;
using sdl_rdp::sample_gate_test::process::Process;
using sdl_rdp::sample_gate_test::process::Window;
using sdl_rdp::sample_gate_test::sample::Arguments;
using sdl_rdp::sample_gate_test::sample::BackendLibrary;
using sdl_rdp::sample_gate_test::sample::BuildRoot;
using sdl_rdp::sample_gate_test::sample::PrimaryDisplayPort;
using sdl_rdp::sample_gate_test::sample::Sample;
using sdl_rdp::sample_gate_test::sample::Words;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Mode;
using sdl_rdp::utilities::Narrowed;

namespace {
using sdl_rdp::settings::Aspect;
using sdl_rdp::settings::Extent;
using sdl_rdp::settings::HintName;
using sdl_rdp::settings::Kilobits;
using sdl_rdp::settings::Milliseconds;
using sdl_rdp::settings::Port;
using sdl_rdp::settings::Refresh;
using sdl_rdp::settings::Settings;
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
  auto const bound = bind(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  Expects(bound == 0, "ephemeral port bound");
  socklen_t  size  = sizeof(address);
  auto const named = getsockname(socket_fd, reinterpret_cast<sockaddr*>(&address), &size);
  Expects(named == 0, "port obtained");
  close(socket_fd);
  return ntohs(address.sin_port);
}
auto SettingsArguments(std::filesystem::path const& directory, std::filesystem::path const& certificates,
                       Words const& environment = { }, Words const& options = { }) -> Words {
  auto args = Arguments(certificates, environment, options);
  std::erase_if(
      args, [](auto const& arg) { return arg.starts_with("SDL_RDP_PORT=") || arg.starts_with("SDL_RDP_BACKEND="); });
  args.insert(args.begin() + 1, { "-u", "SDL_RDP_PORT", "-u", "SDL_RDP_BACKEND", "-u", "SDL_RDP_SETTINGS", "-u",
                                  "SDL_RDP_ASPECT", "-C", directory.string() });
  return args;
}
auto WrittenText(std::filesystem::path const& file, std::string_view text) -> std::filesystem::path {
  std::ofstream out(file);
  out << text;
  bool const written = !out.fail();
  Expects(written, "settings text written");
  return file;
}
auto WriteInvalidSettings(std::filesystem::path const& directory) -> void {
  WrittenText(directory / "libSDL3.yaml", "port: 1\nauth: invalid\n");
}
auto Served(std::uint32_t port) -> Settings {
  return { .backend = BackendLibrary().string(),
           .port    = Port{ Narrowed<std::uint16_t>(port) },
           .aspect  = Aspect{ 4, 3 } };
}
auto WriteSettings(std::filesystem::path const& file, Settings const& settings) -> void {
  oxbox::serialization::SerializeTo(settings, file);
}
}
TEST(SettingsHints, EveryFieldsHintIsThePatchesMacro) {
  EXPECT_EQ(oxbox::serialization::FieldNames(Settings{ }).size(), 18U);
  EXPECT_EQ(HintName<&Settings::backend>(), SDL_HINT_RDP_BACKEND);
  EXPECT_EQ(HintName<&Settings::bind>(), SDL_HINT_RDP_BIND);
  EXPECT_EQ(HintName<&Settings::port>(), SDL_HINT_RDP_PORT);
  EXPECT_EQ(HintName<&Settings::cert_dir>(), SDL_HINT_RDP_CERT_DIR);
  EXPECT_EQ(HintName<&Settings::width>(), SDL_HINT_RDP_WIDTH);
  EXPECT_EQ(HintName<&Settings::height>(), SDL_HINT_RDP_HEIGHT);
  EXPECT_EQ(HintName<&Settings::refresh>(), SDL_HINT_RDP_REFRESH);
  EXPECT_EQ(HintName<&Settings::aspect>(), SDL_HINT_RDP_ASPECT);
  EXPECT_EQ(HintName<&Settings::codec>(), SDL_HINT_RDP_CODEC);
  EXPECT_EQ(HintName<&Settings::avc_bitrate>(), SDL_HINT_RDP_AVC_BITRATE);
  EXPECT_EQ(HintName<&Settings::vsync>(), SDL_HINT_RDP_VSYNC);
  EXPECT_EQ(HintName<&Settings::wait_for_client>(), SDL_HINT_RDP_WAIT_FOR_CLIENT);
  EXPECT_EQ(HintName<&Settings::audio_latency>(), SDL_HINT_RDP_AUDIO_LATENCY);
  EXPECT_EQ(HintName<&Settings::audio_lead>(), SDL_HINT_RDP_AUDIO_LEAD);
  EXPECT_EQ(HintName<&Settings::user>(), SDL_HINT_RDP_USER);
  EXPECT_EQ(HintName<&Settings::password>(), SDL_HINT_RDP_PASSWORD);
  EXPECT_EQ(HintName<&Settings::domain>(), SDL_HINT_RDP_DOMAIN);
  EXPECT_EQ(HintName<&Settings::auth>(), SDL_HINT_RDP_AUTH);
}
class SettingsSample : public Sample, public testing::WithParamInterface<bool> { };
namespace {
auto ThenLiveAspect(SDL_Window* window) -> void {
  auto properties = SDL_GetWindowProperties(window);
  EXPECT_STREQ(SDL_GetStringProperty(properties, SDL_PROP_WINDOW_RDP_ASPECT_STRING, ""), "4:3");
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_ASPECT, "2:1"));
  EXPECT_STREQ(SDL_GetStringProperty(properties, SDL_PROP_WINDOW_RDP_ASPECT_STRING, ""), "2:1");
  ThenAspectReset(properties);
}
}
TEST_P(SettingsSample, WorkingDirectoryFileWinsOverEnvironment) {
  oxbox::platform::ScratchArea const directory { "settings", "sdl-rdp" };
  auto                               port      = AvailablePort();
  WriteSettings(directory.Path() / "libSDL3.yaml", Served(port));
  auto const overridden = GetParam() ? Words{ "SDL_RDP_PORT=1", "SDL_RDP_BACKEND=/missing/backend" } : Words{ };
  ThenSettingsConnect(SettingsArguments(directory.Path(), certificates.Path(), overridden), port);
}
INSTANTIATE_TEST_SUITE_P(SettingsPrecedence, SettingsSample, testing::Bool());

class SettingsChoice : public Sample {
protected:
  auto ThenChosenOverWorkingDirectory(std::filesystem::path const& chosen, std::filesystem::path const& directory,
                                      Words const& environment) -> void {
    auto const port = AvailablePort();
    WriteSettings(chosen, Served(port));
    WriteInvalidSettings(directory);
    ThenSettingsConnect(SettingsArguments(directory, certificates.Path(), environment), port);
  }
};
TEST_F(SettingsChoice, ExplicitPathWinsInAnyFormat) {
  oxbox::platform::ScratchArea const directory{ "settings-explicit", "sdl-rdp" };
  ThenChosenOverWorkingDirectory(directory.Path() / "chosen.json", directory.Path(),
                                 { "SDL_RDP_SETTINGS=chosen.json" });
}

TEST_F(Sample, SettingsUnreadableExplicitPathFailsStartup) {
  oxbox::platform::ScratchArea const directory{ "settings-missing", "sdl-rdp" };
  WriteSettings(directory.Path() / "libSDL3.yaml", Served(0));
  process = std::make_unique<Process>(
      SettingsArguments(directory.Path(), certificates.Path(), { "SDL_RDP_SETTINGS=missing.yaml" }));
  ASSERT_TRUE(Read("ERROR: Could not read RDP settings file missing.yaml")) << process->Transcript();
  EXPECT_FALSE(process->Transcript().contains("port "));
}

TEST_F(Sample, SettingsTwoFilesAtOneLocationFailStartup) {
  oxbox::platform::ScratchArea const directory{ "settings-two", "sdl-rdp" };
  WriteSettings(directory.Path() / "libSDL3.yaml", Served(0));
  WriteSettings(directory.Path() / "libSDL3.json", Served(0));
  process = std::make_unique<Process>(SettingsArguments(directory.Path(), certificates.Path()));
  ASSERT_TRUE(Read("ERROR: RDP settings files ")) << process->Transcript();
  EXPECT_TRUE(process->Transcript().contains("libSDL3.json and libSDL3.yaml are both present"))
      << process->Transcript();
}
namespace {
auto EverySetting(std::filesystem::path const& certificates, std::uint32_t port) -> Settings {
  auto settings = Served(port);
  settings.bind.emplace("127.0.0.1");
  settings.cert_dir.emplace(certificates.string());
  settings.width.emplace(1024);
  settings.height.emplace(768);
  settings.refresh.emplace(Refresh::Rate{ 60 });
  settings.codec.emplace(SDLRDP_CODEC_PLANAR);
  settings.avc_bitrate.emplace(0);
  settings.vsync.emplace(false);
  settings.wait_for_client.emplace(false);
  settings.audio_latency.emplace(500);
  settings.audio_lead.emplace(150);
  settings.user.emplace("alice");
  settings.password.emplace("secret");
  settings.domain.emplace("example");
  settings.auth.emplace(SDLRDP_AUTH_NONE);
  return settings;
}
auto ThenEveryFieldSet(Settings const& settings) -> void {
  oxbox::serialization::ForEachField(
      settings, [](auto const& field, auto const& value) { EXPECT_TRUE(value.has_value()) << field.Name(); });
}
}
TEST_F(Sample, SettingsNamingEveryFieldStartsAndConnects) {
  oxbox::platform::ScratchArea const directory { "settings-every", "sdl-rdp" };
  auto const                         port      = AvailablePort();
  auto const                         settings  = EverySetting(certificates.Path(), port);
  ASSERT_NO_FATAL_FAILURE(ThenEveryFieldSet(settings));
  WriteSettings(directory.Path() / "libSDL3.yml", settings);
  ThenSettingsConnect(SettingsArguments(directory.Path(), certificates.Path()), port);
}

TEST_F(Sample, SettingsTypedValuesAndAnEmptyValueIsAbsent) {
  oxbox::platform::ScratchArea const directory { "settings-typed", "sdl-rdp" };
  auto const                         port      = AvailablePort();
  WrittenText(directory.Path() / "libSDL3.yaml",
              std::format("port: {}\nbackend: {}\naspect: 4:3\ncodec: planar\nwait_for_client: false\ndomain:\n", port,
                          BackendLibrary().string()));
  ThenSettingsConnect(SettingsArguments(directory.Path(), certificates.Path()), port);
}
TEST_F(Sample, SettingsApplicationHintWins) {
  oxbox::platform::ScratchArea const directory{ "settings-hint", "sdl-rdp" };
  WriteSettings(directory.Path() / "libSDL3.yaml", Served(0));
  ASSERT_NO_FATAL_FAILURE(Launch(SettingsArguments(directory.Path(), certificates.Path(), { }, { "--aspect", "2:1" })));
  auto client = AnnouncedClient(640, 480);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(client.Until([&] { return client.Instance()->context->gdi->width == 960; }));
  EXPECT_EQ(client.Instance()->context->gdi->height, 480);
  Escape(client);
}

TEST_F(SettingsChoice, LibraryDirectoryWinsOverWorkingDirectory) {
  oxbox::platform::ScratchArea const directory { "settings-library", "sdl-rdp" };
  auto                               library   = directory.Path() / "library";
  std::filesystem::create_directory(library);
  std::filesystem::create_symlink(BuildRoot() / "sources/sdl-rdp/SDL3/libSDL3.so.0", library / "libSDL3.so.0");
  ThenChosenOverWorkingDirectory(library / "libSDL3.yaml", directory.Path(), { "LD_LIBRARY_PATH=" + library.string() });
}
TEST_F(Sample, SettingsCodeHintsCacheAndLiveReset) {
  oxbox::platform::ScratchArea const directory { "settings-cache", "sdl-rdp" };
  auto                               file      = directory.Path() / "libSDL3.yaml";
  WriteSettings(file, Served(1));
  ASSERT_NO_FATAL_FAILURE(GivenSettingsHints(file));
  EXPECT_GT(PrimaryDisplayPort(), 2u);
  auto* window = SDL_CreateWindow("settings", 640, 480, 0);
  ASSERT_NE(window, nullptr) << SDL_GetError();
  ASSERT_NO_FATAL_FAILURE(ThenLiveAspect(window));
  SDL_DestroyWindow(window);
  SDL_Quit();
  auto reloaded = Served(1);
  reloaded.aspect.emplace(2, 1);
  WriteSettings(file, reloaded);
  ThenReloadedSettings(file);
}
namespace {
using Storage = std::unique_ptr<SDL_Storage, decltype(&SDL_CloseStorage)>;
class SettingsFile {
public:
  explicit SettingsFile(char const* name) : _directory{ name, "sdl-rdp" }, _path{ _directory.Path() / "libSDL3.yaml" } {
    WriteSettings(_path, Served(1));
  }
  auto Path() const -> std::filesystem::path const& {
    return _path;
  }
private:
  oxbox::platform::ScratchArea const _directory;
  std::filesystem::path const        _path;
};
auto RdpTitleStorage() -> Storage {
  EXPECT_TRUE(SDL_SetHint(SDL_HINT_STORAGE_TITLE_DRIVER, "rdp"));
  return { SDL_OpenTitleStorage("", 0), SDL_CloseStorage };
}
auto WindowAspect(Window const& window) -> std::string {
  return SDL_GetStringProperty(SDL_GetWindowProperties(window.get()), SDL_PROP_WINDOW_RDP_ASPECT_STRING, "");
}
auto ThenVideoRejoinsDriver(std::filesystem::path const& file) -> void {
  auto const port     = PrimaryDisplayPort();
  auto       reloaded = Served(1);
  reloaded.aspect.emplace(2, 1);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  WriteSettings(file, reloaded);
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
  return Process{ { "env", std::string(StorageSpaceChild) + "=1",
                    std::filesystem::read_symlink("/proc/self/exe").string(), "--gtest_filter=" + CurrentTest() } };
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
  if constexpr (Mode() == oxbox::platform::ContractMode::STOP)
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
class SettingsSession : public Sample {
protected:
  auto SetUp() -> void override {
    ASSERT_NO_FATAL_FAILURE(Sample::SetUp());
    _sdl.emplace([this] {
      GivenSettingsHints(_file.Path());
      return true;
    });
  }
  auto FilePath() const -> std::filesystem::path const& {
    return _file.Path();
  }
private:
  SettingsFile const            _file{ testing::UnitTest::GetInstance()->current_test_info()->name() };
  std::optional<InitializedSdl> _sdl;
};
TEST_F(SettingsSession, PartialVideoQuitKeepsDriverAndSettingsSnapshot) {
  Storage const storage = RdpTitleStorage();
  ASSERT_TRUE(storage) << SDL_GetError();
  ASSERT_NO_FATAL_FAILURE(ThenVideoRejoinsDriver(FilePath()));
  ThenWindowAspect("4:3");
}
TEST_F(SettingsSession, InvalidAspectHintFailsWindowCreationAndCanRecover) {
  for (auto const* aspect : { "1:0", "4:3:2", "4:x", "4 : 3" }) ThenInvalidAspectFailsWindow(aspect);
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_ASPECT, "4:3"));
  ThenWindowAspect("4:3");
}
TEST_F(SettingsSession, StorageSpaceDiagnosesUnsupportedBackendOperation) {
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
using FileAndCause = std::pair<std::string_view, std::string_view>;
class InvalidFile : public Sample, public testing::WithParamInterface<FileAndCause> { };
TEST_P(InvalidFile, FailsInitOnceNamingTheFileAndTheCause) {
  oxbox::platform::ScratchArea const directory { "settings-invalid", "sdl-rdp" };
  auto const                         file      = WrittenText(directory.Path() / "libSDL3.yaml", GetParam().first);
  InitializedSdl const               sdl       { [&] {
    WhenInitializedWith({ SDL_HINT_RDP_SETTINGS, file.c_str() });
    return true;
  } };
  std::string_view const             error     { SDL_GetError()                };
  EXPECT_TRUE(error.contains("Invalid RDP settings file " + file.string())) << error;
  EXPECT_TRUE(error.contains(GetParam().second)) << error;
}
INSTANTIATE_TEST_SUITE_P(Settings, InvalidFile,
                         testing::Values(FileAndCause{ "port: 1\ncolour: blue\n", "unknown key 'colour'" },
                                         FileAndCause{ "port: [1\n", "line" }, FileAndCause{ "port: 70000\n", "70000" },
                                         FileAndCause{ "codec: avc\n", "avc"                     },
                                         FileAndCause{ "aspect: 4:0\n", "'4:0' is not an aspect" },
                                         FileAndCause{ "refresh: auto\n", "'auto' is not a refresh" }));
}
