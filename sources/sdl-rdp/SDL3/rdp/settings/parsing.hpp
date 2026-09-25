#pragma once
#include <sdl-rdp/SDL3/rdp/exceptions.hpp>
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/settings/parsed.hpp>
#include <sdl-rdp/settings/settings.hpp>
#include <sdl-rdp/utilities/bounded.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <oxbox/serialization/io.hpp>
#include <oxbox/utilities/number-text.hpp>
#include <oxbox/utilities/text.hpp>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace sdl3::rdp::settings::detail::parsing {
using sdl_rdp::settings::JoinedNames;
using sdl_rdp::settings::ParsedFromText;
using sdl_rdp::settings::ParsedOr;
using sdl_rdp::utilities::BoundedInteger;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Reported;

auto ReportInvalidSetting(std::string_view text) -> void;
// A setting the user wrote wrongly is logged where the user looks, then fails the call that read it.
template <typename FailureTy, typename... ArgsTy>
[[noreturn]] auto InvalidSetting(ArgsTy const&... args) -> void {
  throw Reported(FailureTy{ args... }, ReportInvalidSetting);
}
// A hint or environment value, read as its settings field's type; a value the type refuses names the hint.
auto FromText([[maybe_unused]] std::type_identity<std::string> type, std::string_view name, std::string const& text)
    -> std::string;
auto FromText([[maybe_unused]] std::type_identity<bool> type, std::string_view name, std::string const& text) -> bool;
template <oxbox::serialization::HasEnumMap EnumTy>
auto FromText([[maybe_unused]] std::type_identity<EnumTy> type, std::string_view name, std::string const& text)
    -> EnumTy {
  Expects(!name.empty(), "a setting has a hint name");
  auto const value = oxbox::serialization::FromString<EnumTy>(text);
  if (!value) InvalidSetting<UnknownName>(name, text, JoinedNames<EnumTy>());
  return *value;
}
template <BoundedInteger BoundedTy>
auto FromText([[maybe_unused]] std::type_identity<BoundedTy> type, std::string_view name, std::string const& text)
    -> BoundedTy {
  Expects(!name.empty(), "a setting has a hint name");
  auto const number = oxbox::utilities::ParseNumber<std::int64_t>(oxbox::utilities::Trimmed(text));
  if (!number || !BoundedTy::Admits(*number))
    InvalidSetting<IntegerOutOfRange>(name, text, BoundedTy::Minimum(), BoundedTy::Maximum());
  return BoundedTy{ static_cast<typename BoundedTy::value_type>(*number) };
}
template <ParsedFromText ValueTy>
auto FromText([[maybe_unused]] std::type_identity<ValueTy> type, std::string_view name, std::string const& text)
    -> ValueTy {
  Expects(!name.empty(), "a setting has a hint name");
  return ParsedOr<ValueTy>(text, [&] -> ValueTy { InvalidSetting<InvalidText>(name, text, ValueTy::Form()); });
}
}

namespace sdl3::rdp::settings {
using detail::parsing::FromText;
using detail::parsing::InvalidSetting;
using detail::parsing::ReportInvalidSetting;
}
