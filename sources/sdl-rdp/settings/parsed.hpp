#pragma once
#include <sdl-rdp/settings/exceptions.hpp>

#include <concepts>
#include <optional>
#include <string_view>
#include <type_traits>

namespace sdl_rdp::settings::detail::parsed {
// A setting type written as text: Parsed() reads it, Form() says what it expects.
template <typename ValueTy>
concept ParsedFromText = requires(std::string_view text) {
  { ValueTy::Parsed(text) } -> std::same_as<std::optional<ValueTy>>;
  { ValueTy::Form() } -> std::same_as<std::string_view>;
};
// The value the text reads as, else what the refusal returns; a refusal throws, so it returns the type.
template <ParsedFromText ValueTy, std::invocable RefusalTy>
  requires std::same_as<std::invoke_result_t<RefusalTy>, ValueTy>
auto ParsedOr(std::string_view text, RefusalTy refusal) -> ValueTy {
  auto const value = ValueTy::Parsed(text);
  return value ? *value : refusal();
}
template <ParsedFromText ValueTy>
auto ParsedOrRefused(std::string_view text) -> ValueTy {
  return ParsedOr<ValueTy>(text, [&] -> ValueTy { throw InvalidSettingValue{ text, ValueTy::Form() }; });
}
}
namespace sdl_rdp::settings {
using detail::parsed::ParsedFromText;
using detail::parsed::ParsedOr;
using detail::parsed::ParsedOrRefused;
}
