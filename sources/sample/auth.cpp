#include <sample/auth.hpp>

#include <sample/check.hpp>

#include <oxbox/utilities/hash.hpp>
#include <optional>
#include <string>

namespace sample::detail::auth {
using oxbox::utilities::HashString;
using oxbox::utilities::literals::operator""_hash;

namespace {
// The SDL hint an authentication option sets, if the option is one.
auto HintOf(std::string_view option) -> std::optional<std::string_view> {
  switch (HashString(option)) {
  case "--user"_hash:     return SDL_HINT_RDP_USER;
  case "--password"_hash: return SDL_HINT_RDP_PASSWORD;
  case "--domain"_hash:   return SDL_HINT_RDP_DOMAIN;
  case "--auth"_hash:     return SDL_HINT_RDP_AUTH;
  default:                return std::nullopt;
  }
}
}
auto SDLCALL Authenticator::Deny(void* /*unused*/, char const* /*unused*/, char const* /*unused*/,
                                 char const* /*unused*/) -> bool {
  return false;
}
auto SDLCALL Authenticator::AuthenticationLog(void* user, int category, SDL_LogPriority priority, char const* message)
    -> void {
  Check(user != nullptr);
  Check(message != nullptr);
  auto&                      self   = *static_cast<Authenticator*>(user);
  std::string_view           text(message);
  constexpr std::string_view prefix = "Authentication rejected: user \"";
  self.previous(self.previous_user, category, priority, message);
  if (!text.starts_with(prefix)) return;
  text.remove_prefix(prefix.size());
  auto end  = text.find('"');
  auto name = text.substr(0, end);
  if (auto slash = name.find('\\'); slash != std::basic_string_view<char, std::char_traits<char>>::npos)
    name.remove_prefix(slash + 1);
  auto line = std::string("event AUTH_REJECTED user=") + std::string(name);
  self.previous(self.previous_user, category, SDL_LOG_PRIORITY_INFO, line.c_str());
}
auto Authenticator::Option(std::span<std::string_view const> arguments, std::size_t& index) -> bool {
  auto const option = arguments[index];
  if (option == "--verify-deny") {
    deny = true;
    return true;
  }
  auto const hint = HintOf(option);
  if (!hint) return false;
  Check(index + 1 < arguments.size());
  Check(SDL_SetHint(std::string{ *hint }.c_str(), std::string{ arguments[++index] }.c_str()));
  return true;
}
auto Authenticator::Defaults() const -> void {
  if (deny && !SDL_GetHint(SDL_HINT_RDP_AUTH) && !SDL_GetHint(SDL_HINT_RDP_PASSWORD))
    Check(SDL_SetHint(SDL_HINT_RDP_AUTH, "tls"));
}
auto Authenticator::Install() -> void {
  SDL_SetLogPriority(SDL_LOG_CATEGORY_VIDEO, SDL_LOG_PRIORITY_INFO);
  SDL_GetLogOutputFunction(&previous, &previous_user);
  SDL_SetLogOutputFunction(AuthenticationLog, this);
  if (deny)
    Check(SDL_SetPointerProperty(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()), SDL_PROP_DISPLAY_RDP_VERIFY_POINTER,
                                 reinterpret_cast<void*>(Deny)));
}
Authenticator::~Authenticator() {
  if (previous) SDL_SetLogOutputFunction(previous, previous_user);
}
auto PrintAuthentication(SDL_Window& window) -> void {
  auto properties = SDL_GetWindowProperties(&window);
  SDL_Log("event CONNECTED user=%s domain=%s authenticated=%d",
          SDL_GetStringProperty(properties, SDL_PROP_WINDOW_RDP_USER_STRING, ""),
          SDL_GetStringProperty(properties, SDL_PROP_WINDOW_RDP_DOMAIN_STRING, ""),
          int{ SDL_GetBooleanProperty(properties, SDL_PROP_WINDOW_RDP_AUTHENTICATED_BOOLEAN, false) });
}
}
