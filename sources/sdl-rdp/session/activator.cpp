#include <sdl-rdp/session/activator.hpp>

#include <sdl-rdp/auth/auth.hpp>
#include <sdl-rdp/core/activation.hpp>
#include <sdl-rdp/core/configuration.hpp>
#include <sdl-rdp/core/peer-link.hpp>
#include <sdl-rdp/session/arrival.hpp>
#include <sdl-rdp/video/encoder.hpp>

#include <freerdp/session.h>
#include <freerdp/settings.h>
#include <freerdp/update.h>
#include <winpr/crypto.h>
#include <algorithm>

namespace Backend {
namespace {
constexpr UINT32 CookieLength   = 28;
constexpr UINT32 SessionLogonId = 1;
auto SendCookie(rdpContext& context) -> bool {
  ARC_SC_PRIVATE_PACKET cookie{ };
  cookie.cbLen   = CookieLength;
  cookie.version = AUTO_RECONNECT_VERSION_1;
  cookie.logonId = SessionLogonId;
  if (winpr_RAND(cookie.arcRandomBits, sizeof(cookie.arcRandomBits)) != 0) return false;
  if (!freerdp_settings_set_pointer_len(context.settings, FreeRDP_ServerAutoReconnectCookie, &cookie, 1)) return false;
  logon_info_ex info{ };
  info.haveCookie = TRUE;
  info.LogonId    = cookie.logonId;
  std::ranges::copy(cookie.arcRandomBits, info.ArcRandomBits);
  return context.update->SaveSessionInfo(&context, INFO_TYPE_LOGON_EXTENDED_INF, &info);
}
}
Activator::Activator(PeerLink& link, Authenticator& authenticator, Activation const& activation, Encoder& encoder,
                     Configuration const& configuration, Arrival& arrival) noexcept
    : _link{ link }, _authenticator{ authenticator }, _activation{ activation }, _encoder{ encoder },
      _configuration{ configuration }, _arrival{ arrival } { }
auto Activator::Activate() -> BOOL {
  if (_activation.Active()) {
    _link.Signal();
    return TRUE;
  }
  if (!_authenticator.VerifySettings() || !SendCookie(_link.Context())) return FALSE;
  if (!_encoder.Select(&_link.Settings(), _configuration.Codec())) return FALSE;
  _arrival.Admit(_encoder.Codec());
  return TRUE;
}
}
