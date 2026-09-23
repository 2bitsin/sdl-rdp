#pragma once

#include <algorithm>
#include <array>
#include <oxbox/platform/contract.hpp>
#include <source_location>
#include <string_view>

namespace utilities::detail::contract {
consteval auto Mode() -> oxbox::platform::ContractMode {
  using enum oxbox::platform::ContractMode;
  constexpr std::array names{ std::string_view{ "stop" }, std::string_view{ "complain" },
                              std::string_view{ "ignore" } };
  constexpr auto found = std::ranges::find(names, std::string_view{ BACKEND_CONTRACTS }) - names.begin();
  static_assert(found != names.size(), "unknown BACKEND_CONTRACTS word");
  return std::array{ STOP, COMPLAIN, IGNORE }[found];
}
using Checked = oxbox::platform::Contracts<Mode()>;

inline auto Expects(bool held, std::string_view text, std::source_location where = std::source_location::current())
    -> void {
  Checked::Expects(held, text, where);
}

inline auto Ensures(bool held, std::string_view text, std::source_location where = std::source_location::current())
    -> void {
  Checked::Ensures(held, text, where);
}

template <typename VTy>
[[noreturn]] auto Unreachable(VTy const& value, std::source_location where = std::source_location::current()) -> void {
  oxbox::platform::Contracts<oxbox::platform::ContractMode::STOP>::Unreachable(value, where);
}

inline auto NotImplemented(std::string_view text, std::source_location where = std::source_location::current())
    -> void {
  Checked::NotImplemented(text, where);
}
}

namespace utilities {
using detail::contract::Ensures;
using detail::contract::Expects;
using detail::contract::NotImplemented;
using detail::contract::Unreachable;
}
