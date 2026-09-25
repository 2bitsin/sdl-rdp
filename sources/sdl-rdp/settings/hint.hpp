#pragma once
#include <sdl-rdp/settings/settings.hpp>
#include <sdl-rdp/utilities/ascii.hpp>

#include <_buildutil/reflect.hpp>
#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <type_traits>

namespace sdl_rdp::settings::detail::hint {
using sdl_rdp::utilities::AsciiUpper;

inline constexpr std::string_view HintPrefix = "SDL_RDP_";
template <typename MemberTy, auto FIELD>
consteval auto Names() -> bool {
  if constexpr (std::same_as<std::remove_cv_t<decltype(MemberTy::REFERENCE)>, decltype(FIELD)>)
    return MemberTy::REFERENCE == FIELD;
  else
    return false;
}
template <auto FIELD, typename... MemberTy>
consteval auto NameIn([[maybe_unused]] ::reflect::class_scheme<MemberTy...> scheme) -> std::string_view {
  std::string_view name;
  ((name = Names<MemberTy, FIELD>() ? MemberTy::NAME_STRING : name), ...);
  return name;
}
// The settings file's key for a field, read off the generated scheme.
template <auto FIELD>
consteval auto FieldName() -> std::string_view {
  constexpr auto name = NameIn<FIELD>(::reflect::scheme_of<Settings>());
  static_assert(!name.empty(), "the field is in the settings scheme");
  return name;
}
// The hint's text and a terminating zero, so SDL's C string APIs read it in place.
template <std::size_t LENGTH>
consteval auto Spelled(std::string_view field) -> std::array<char, HintPrefix.size() + LENGTH + 1> {
  std::array<char, HintPrefix.size() + LENGTH + 1> text{ };
  std::ranges::copy(HintPrefix, text.begin());
  std::ranges::transform(field, text.begin() + HintPrefix.size(), AsciiUpper);
  return text;
}
template <auto FIELD>
inline constexpr auto HintText = Spelled<FieldName<FIELD>().size()>(FieldName<FIELD>());
// SDL_RDP_ and the field's key in capitals: the hint and the environment variable that set the field.
template <auto FIELD>
constexpr auto HintName() -> std::string_view {
  return { HintText<FIELD>.data(), HintText<FIELD>.size() - 1 };
}
}

namespace sdl_rdp::settings {
using detail::hint::FieldName;
using detail::hint::HintName;
using detail::hint::HintText;
}
