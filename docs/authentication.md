# Authentication

`SDL_RDP_AUTH` selects `none`, `tls`, or `nla`. `none` preserves the default:
TLS and standard RDP security, with no credential checks. `tls` accepts only TLS
and verifies the plain credentials in Client Info. `nla` offers CredSSP/NTLM
and also accepts TLS-only clients; NLA checks the NT hash first, then verifies
the delegated plain credentials. A password hint defaults the driver to `nla`;
without it the default is `none`. Backend configs default to `SDLRDP_AUTH_NONE`.

Set `SDL_HINT_RDP_USER`, `SDL_HINT_RDP_PASSWORD`, and optionally
`SDL_HINT_RDP_DOMAIN` before initializing audio or video (environment names
`SDL_RDP_USER`, `SDL_RDP_PASSWORD`, `SDL_RDP_DOMAIN`). An unset domain accepts
any domain; a set domain must match exactly.
The `SDL_RDP_PASSWORD` environment variable is readable by other processes of the same user; set the password hint from code or use a permission-restricted ini file as an alternative. Set `SDL_HINT_RDP_AUTH` to override
the default. The sample accepts `--user <u> --password <p> [--domain <d>]
[--auth none|tls|nla]`; `--verify-deny` exercises application rejection.

After `SDL_Init`, an app can set these display pointer properties:

- `SDL_PROP_DISPLAY_RDP_VERIFY_POINTER`:
  `bool (SDLCALL *)(void *userdata, const char *domain, const char *user, const char *password)`.
- `SDL_PROP_DISPLAY_RDP_LOOKUP_POINTER`:
  `bool (SDLCALL *)(void *userdata, const char *domain, const char *user, Uint8 nt_hash[16])`.
- `SDL_PROP_DISPLAY_RDP_AUTH_USERDATA_POINTER`: shared callback userdata.

Callbacks run on the peer worker thread. The driver reads properties on each
call. Keep callbacks and userdata alive until shutdown and synchronize mutable
application state. Callbacks take precedence over the fixed hint pair. In TLS
and NLA modes, missing verification rejects; missing lookup rejects NLA while
TLS-only clients still reach verification. `none` does not invoke verification.
Use an explicit authentication hint with property callbacks. The listener opens
during init; install properties before allowing clients to connect. Audio-only
use supports the fixed hints but has no display for callback properties.

On connection, read `SDL_PROP_WINDOW_RDP_USER_STRING`,
`SDL_PROP_WINDOW_RDP_DOMAIN_STRING`, and
`SDL_PROP_WINDOW_RDP_AUTHENTICATED_BOOLEAN`. Names are UTF-8; `none` still reports
the Client Info name with authenticated=false. Passwords never enter events or
logs. Rejections produce one backend WARN `Authentication rejected: user
"<domain\user>" from <address>`; the sample also prints `event AUTH_REJECTED
user=<u>`. Successful sample sessions print `event CONNECTED user=<u>
domain=<d> authenticated=<0|1>`.

mstsc and the Mac Remote Desktop client prompt for NLA credentials.
With xfreerdp use `/sec:nla` or `/sec:tls` and `/u:`, `/p:`, `/d:`.
There is no lockout, PAM integration, or Kerberos authentication.

Backend ABI 6 adds authentication and identity fields. The former log userdata
field is now `log_user`; `user` is the fixed username. Config credential strings
are copied by open. Callback pointers and `auth_user` must live through close.
`sdlrdp_verify_pair` and `sdlrdp_lookup_pair` expose the same fixed-pair fallback
for the dynamically loaded SDL driver.
