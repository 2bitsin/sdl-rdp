#pragma once
#include <sdl-rdp/utilities/releases.hpp>

#include <freerdp/settings.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::settings {
using Settings = std::unique_ptr<rdpSettings, Backend::Releases<freerdp_settings_free>>;
template <std::ranges::input_range EntriesTy>
using KeyOf = typename std::ranges::range_value_t<EntriesTy>::first_type;

auto Set(rdpSettings& settings, FreeRDP_Settings_Keys_Bool key, bool value)               -> bool;
auto Set(rdpSettings& settings, FreeRDP_Settings_Keys_UInt32 key, std::uint32_t value)    -> bool;
auto Set(rdpSettings& settings, FreeRDP_Settings_Keys_String key, std::string_view value) -> bool;
auto KeyName(std::ptrdiff_t key)                                                          -> std::string_view;

// Sets each entry in order and stops at the first key FreeRDP refuses, which it returns.
template <std::ranges::input_range EntriesTy>
auto FirstRefused(rdpSettings& settings, EntriesTy const& entries) -> std::optional<KeyOf<EntriesTy>> {
  auto const refused = std::ranges::find_if_not(
      entries, [&](auto const& entry) { return Set(settings, entry.first, entry.second); });
  if (refused == std::ranges::end(entries)) return std::nullopt;
  return refused->first;
}

template <typename KeyTy>
auto Refusal(std::string_view subject, std::optional<KeyTy> refused) -> std::string {
  if (!refused) return std::string{ subject };
  return std::format("{}: FreeRDP refused {}", subject, KeyName(std::to_underlying(*refused)));
}
}

namespace sdl_rdp::freerdp_facade {
using detail::settings::FirstRefused;
using detail::settings::KeyName;
using detail::settings::Refusal;
using detail::settings::Set;
using detail::settings::Settings;
}
