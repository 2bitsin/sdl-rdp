#pragma once
#include "defaults.hpp"
#include "parsing.hpp"
#include <sdl-rdp/SDL3/rdp/backend/sdl-internals.hpp>
#include <sdl-rdp/settings/hint.hpp>
#include <sdl-rdp/settings/settings.hpp>

#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace sdl3::rdp::settings::detail::options {
using sdl_rdp::settings::HintName;
using sdl_rdp::settings::HintText;
using sdl_rdp::settings::Settings;
template <auto FIELD>
using FieldValue = std::remove_cvref_t<decltype(*(std::declval<Settings>().*FIELD))>;
// SDL hint and environment APIs return nullable borrowed C strings.
auto Text(char const* text) -> std::optional<std::string>;
// Each setting is the application's hint, else the settings file, else the environment variable of the hint's name.
class Options {
public:
  Options();
  template <auto FIELD>
  auto Get() const -> std::optional<FieldValue<FIELD>> {
    if (auto const hint = Text(SDL_GetHintFromCode(HintText<FIELD>.data())))
      return FromText(std::type_identity<FieldValue<FIELD>>{ }, HintName<FIELD>(), *hint);
    return _Fallback<FIELD>();
  }
  template <auto FIELD>
  auto Value() const -> FieldValue<FIELD> {
    return Get<FIELD>().value_or(Default<FIELD>());
  }
  template <auto FIELD>
  auto Changed(std::optional<std::string> const& old_value, std::optional<std::string> const& new_value) const
      -> FieldValue<FIELD> {
    auto const code = Text(SDL_GetHintFromCode(HintText<FIELD>.data()));
    // SDL_ResetHint calls observers before removing the old code value.
    return old_value != new_value && code && code != new_value ? _Fallback<FIELD>().value_or(Default<FIELD>())
                                                               : Value<FIELD>();
  }
  // Complain mode continues a field without a default at its type's own default.
  template <auto FIELD>
  static auto Default() -> FieldValue<FIELD> {
    auto const& fallback = Defaults().*FIELD;
    utilities::Expects(fallback.has_value(), "the setting has a default");
    return fallback.value_or(FieldValue<FIELD>{ });
  }
private:
  template <auto FIELD>
  auto _Fallback() const -> std::optional<FieldValue<FIELD>> {
    if (auto const& value = _file.*FIELD) return value;
    return Text(SDL_getenv(HintText<FIELD>.data())).transform([](std::string const& text) {
      return FromText(std::type_identity<FieldValue<FIELD>>{ }, HintName<FIELD>(), text);
    });
  }
  Settings _file;
};
}
namespace sdl3::rdp::settings {
using detail::options::FieldValue;
using detail::options::Text;
using detail::options::Options;
}
