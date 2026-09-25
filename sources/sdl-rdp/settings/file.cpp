#include <sdl-rdp/settings/file.hpp>
#include <sdl-rdp/settings/exceptions.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/serialization/errors.hpp>
#include <oxbox/serialization/format-pack.hpp>
#include <oxbox/serialization/io.hpp>
#include <oxbox/serialization/query.hpp>
#include <algorithm>
#include <format>
#include <fstream>
#include <ranges>
#include <span>
#include <type_traits>
#include <variant>

namespace sdl_rdp::settings::detail::file {
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::OutOfRange;

namespace {
using oxbox::serialization::AnyFormat;
using oxbox::serialization::FileOpenError;
using oxbox::serialization::FormatTraits;
using oxbox::serialization::IstreamSource;
using oxbox::serialization::MissingField;
using oxbox::serialization::ParseError;
using oxbox::serialization::TypeMismatch;
using Keys = std::vector<std::string>;
template <typename... FormatTy>
auto ClaimedExtensions([[maybe_unused]] std::type_identity<std::variant<FormatTy...>> pack)
    -> std::vector<std::string_view> {
  std::vector<std::string_view> extensions;
  (extensions.append_range(FormatTraits<FormatTy>::extensions), ...);
  return extensions;
}
auto FormatOf(std::filesystem::path const& path) -> AnyFormat {
  auto const format = oxbox::serialization::FormatFromExtension(path.extension().string());
  if (!format) throw UnknownSettingsFormat{ path.extension().string() };
  return *format;
}
// oxbox's reader skips keys its scheme does not name (2bitsin/oxbox#3), so the document's own keys are read.
template <typename FormatTy>
auto DocumentKeys(std::filesystem::path const& path) -> std::optional<Keys> {
  Expects(!path.empty(), "a settings document has a path");
  std::ifstream stream{ path, std::ios::in | FormatTraits<FormatTy>::openmode };
  if (!stream) throw FileOpenError{ path };
  IstreamSource                                     source{ stream };
  typename FormatTy::template Reader<IstreamSource> reader{ source };
  if (reader.IsNull()) return std::nullopt;
  reader.EnterObject();
  return reader.FieldNames() | std::ranges::to<Keys>();
}
auto RefuseUnknown(std::span<std::string const> keys) -> void {
  Expects(std::ranges::none_of(keys, &std::string::empty), "a document key has a name");
  auto const known   = oxbox::serialization::FieldNames(Settings{ });
  auto const unknown = std::ranges::find_if(keys, [&](auto const& key) { return !std::ranges::contains(known, key); });
  if (unknown != keys.end()) throw UnknownSettingsKey{ *unknown };
}
auto Read(std::filesystem::path const& path) -> Settings {
  auto const keys = std::visit([&]<typename FormatTy>(FormatTy) { return DocumentKeys<FormatTy>(path); },
                               FormatOf(path));
  if (!keys) return { };
  RefuseUnknown(*keys);
  return oxbox::serialization::DeserializeFrom<Settings>(path);
}
// Each named refusal becomes the file's; anything else (oxbox's InvalidArgument, a scheme defect) passes unchanged.
template <typename ErrorTy, typename... RestTy>
auto Refusing(std::filesystem::path const& path) -> Settings {
  try {
    if constexpr (sizeof...(RestTy) == 0)
      return Read(path);
    else
      return Refusing<RestTy...>(path);
  } catch (ErrorTy const& error) {
    throw InvalidSettingsFile{ path.string(), error.what() };
  }
}
}
auto Extensions() -> std::vector<std::string_view> {
  auto extensions = ClaimedExtensions(std::type_identity<AnyFormat>{ });
  std::ranges::sort(extensions);
  auto const duplicates = std::ranges::unique(extensions);
  extensions.erase(duplicates.begin(), duplicates.end());
  Ensures(!extensions.empty(), "oxbox claims an extension");
  return extensions;
}
auto SettingsName(std::filesystem::path const& library) -> std::string {
  auto const file = library.filename().string();
  Expects(!file.empty(), "the library path names a file");
  auto name = file.substr(0, file.find('.'));
  Ensures(!name.empty(), "the library name precedes its first dot");
  return name;
}
auto Located(std::filesystem::path const& directory, std::string_view name) -> std::optional<std::filesystem::path> {
  Expects(!name.empty(), "a settings file has a name");
  auto const candidate = [&](std::string_view extension) { return directory / std::format("{}{}", name, extension); };
  auto const found     = Extensions() | std::views::transform(candidate)
                         | std::views::filter([](auto const& path) { return std::filesystem::exists(path); })
                         | std::ranges::to<std::vector>();
  if (found.size() > 1) throw AmbiguousSettings{ found[0].string(), found[1].string() };
  if (found.empty()) return std::nullopt;
  return found.front();
}
auto Load(std::filesystem::path const& path) -> Settings {
  Expects(!path.empty(), "a settings file has a path");
  return Refusing<ParseError, TypeMismatch, MissingField, FileOpenError, UnknownSettingsFormat, UnknownSettingsKey,
                  InvalidSettingValue, OutOfRange>(path);
}
}
