#include <sdl-rdp/settings/file.hpp>
#include <sdl-rdp/settings/exceptions.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <oxbox/serialization/format-pack.hpp>
#include <oxbox/serialization/io.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

namespace sdl_rdp::settings {
namespace {
auto Written(std::filesystem::path const& path, std::string_view text) -> std::filesystem::path {
  std::ofstream{ path } << text;
  return path;
}
auto EveryField() -> Settings {
  return { .backend         = "/opt/backend.so",
           .bind            = "127.0.0.1",
           .port            = Port{ 3390 },
           .cert_dir        = "/tmp/certificates",
           .width           = Extent{ 640 },
           .height          = Extent{ 480 },
           .refresh         = Refresh{ RefreshMode::CLIENT },
           .aspect          = Aspect{ 4, 3 },
           .codec           = SDLRDP_CODEC_PLANAR,
           .avc_bitrate     = Kilobits{ 8000 },
           .vsync           = true,
           .wait_for_client = false,
           .audio_latency   = Milliseconds{ 500 },
           .audio_lead      = Milliseconds{ 150 },
           .user            = "alice",
           .password        = "secret",
           .domain          = "example",
           .auth            = SDLRDP_AUTH_TLS };
}
auto FailureOf(std::filesystem::path const& path) -> std::string {
  try {
    std::ignore = Load(path);
  } catch (InvalidSettingsFile const& error) {
    return error.what();
  }
  return { };
}
template <typename... FormatTy>
auto EveryFormatListed([[maybe_unused]] std::type_identity<std::variant<FormatTy...>> pack) -> bool {
  auto const listed = [](auto const& claimed) {
    return std::ranges::all_of(claimed, [](auto extension) { return std::ranges::contains(Extensions(), extension); });
  };
  return (listed(oxbox::serialization::FormatTraits<FormatTy>::extensions) && ...);
}
}
TEST(SettingsFile, ExtensionsAreEveryOneOxboxClaimsOnceInOrder) {
  auto const extensions = Extensions();
  EXPECT_TRUE(std::ranges::is_sorted(extensions));
  EXPECT_EQ(std::ranges::adjacent_find(extensions), extensions.end());
  EXPECT_TRUE(EveryFormatListed(std::type_identity<oxbox::serialization::AnyFormat>{ }));
}
TEST(SettingsFile, NameIsTheLibraryFileBeforeItsFirstDot) {
  EXPECT_EQ(SettingsName("/usr/lib/libSDL3.so.0"), "libSDL3");
  EXPECT_EQ(SettingsName("C:/Program Files/app/SDL3.dll"), "SDL3");
  EXPECT_EQ(SettingsName("/Library/libSDL3.0.dylib"), "libSDL3");
}
TEST(SettingsFileDeathTest, NameOfAHiddenFileBreaksTheContract) {
  EXPECT_DEATH(std::ignore = SettingsName("/usr/lib/.hidden"), "precedes its first dot");
}
TEST(SettingsFile, LocatedFindsOneFileAndRefusesTwo) {
  oxbox::platform::ScratchArea const directory{ "located", "sdl-rdp" };
  EXPECT_EQ(Located(directory.Path(), "libSDL3"), std::nullopt);
  auto const yaml = Written(directory.Path() / "libSDL3.yml", "port: 1\n");
  EXPECT_EQ(Located(directory.Path(), "libSDL3"), yaml);
  Written(directory.Path() / "libSDL3.json", "{}");
  EXPECT_THROW(std::ignore = Located(directory.Path(), "libSDL3"), AmbiguousSettings);
}
TEST(SettingsFile, YamlReadsTypedValuesAndLeavesTheRestAbsent) {
  oxbox::platform::ScratchArea const directory{ "typed", "sdl-rdp" };
  auto const settings = Load(Written(directory.Path() / "libSDL3.yaml", "port: 3390\ncodec: planar\nvsync: true\n"
                                                                        "aspect: 4:3\nrefresh: 90\ndomain:\n"));
  EXPECT_EQ(settings, (Settings{ .port    = Port{ 3390 },
                                 .refresh = Refresh{ Refresh::Rate{ 90 } },
                                 .aspect  = Aspect{ 4, 3 },
                                 .codec   = SDLRDP_CODEC_PLANAR,
                                 .vsync   = true }));
}
TEST(SettingsFile, EveryFormatRoundTripsEveryField) {
  oxbox::platform::ScratchArea const directory{ "formats", "sdl-rdp" };
  for (auto const extension : Extensions()) {
    auto const path = directory.Path() / std::format("libSDL3{}", extension);
    oxbox::serialization::SerializeTo(EveryField(), path);
    EXPECT_EQ(Load(path), EveryField()) << extension;
  }
}
TEST(SettingsFile, EmptyFileIsNoSettings) {
  oxbox::platform::ScratchArea const directory{ "empty", "sdl-rdp" };
  EXPECT_EQ(Load(Written(directory.Path() / "libSDL3.yaml", "# nothing set\n")), Settings{ });
}
TEST(SettingsFile, RefusalsNameTheFileAndTheCause) {
  oxbox::platform::ScratchArea const directory { "refused", "sdl-rdp" };
  auto const                         unknown   = Written(directory.Path() / "unknown.yaml", "port: 1\ncolour: blue\n");
  EXPECT_TRUE(FailureOf(unknown).contains("unknown.yaml: unknown key 'colour'")) << FailureOf(unknown);
  auto const range = Written(directory.Path() / "range.yaml", "port: 70000\n");
  EXPECT_TRUE(FailureOf(range).contains("70000 is outside 0..65535")) << FailureOf(range);
  auto const codec = Written(directory.Path() / "codec.yaml", "codec: avc\n");
  EXPECT_TRUE(FailureOf(codec).contains("avc")) << FailureOf(codec);
  auto const aspect = Written(directory.Path() / "aspect.yaml", "aspect: 4:0\n");
  EXPECT_TRUE(FailureOf(aspect).contains("'4:0' is not an aspect")) << FailureOf(aspect);
  auto const refresh = Written(directory.Path() / "refresh.yaml", "refresh: auto\n");
  EXPECT_TRUE(FailureOf(refresh).contains("'auto' is not a refresh")) << FailureOf(refresh);
  EXPECT_FALSE(FailureOf(Written(directory.Path() / "malformed.yaml", "port: [1\n")).empty());
  EXPECT_TRUE(FailureOf(Written(directory.Path() / "libSDL3.ini", "port = 1\n")).contains("'.ini'"));
  EXPECT_TRUE(FailureOf(directory.Path() / "missing.yaml").contains("missing.yaml"));
}
}
