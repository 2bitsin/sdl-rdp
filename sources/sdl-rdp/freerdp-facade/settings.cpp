#include <sdl-rdp/freerdp-facade/settings.hpp>

namespace sdl_rdp::freerdp_facade::detail::settings {
auto Set(rdpSettings& settings, FreeRDP_Settings_Keys_Bool key, bool value) -> bool {
  return freerdp_settings_set_bool(&settings, key, value);
}
auto Set(rdpSettings& settings, FreeRDP_Settings_Keys_UInt32 key, std::uint32_t value) -> bool {
  return freerdp_settings_set_uint32(&settings, key, value);
}
auto Set(rdpSettings& settings, FreeRDP_Settings_Keys_String key, std::string_view value) -> bool {
  return freerdp_settings_set_string_len(&settings, key, value.data(), value.size());
}
auto KeyName(std::ptrdiff_t key) -> std::string_view {
  auto const* name = freerdp_settings_get_name_for_key(key);
  return name ? std::string_view{ name } : std::string_view{ "an unknown setting" };
}
}
