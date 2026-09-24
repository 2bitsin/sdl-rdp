#include "_detail/authentication.hpp"

#include "_detail/contract.hpp"

#include <openssl/crypto.h>

namespace Backend {
Authentication::Authentication(sdlrdp_config const& value)
    : _config { value }, _user{ value.user ? value.user : "" }, _password{ value.password ? value.password : "" },
      _domain{ value.domain ? value.domain : "" } {
  Expects(value.auth >= SDLRDP_AUTH_NONE, "authentication mode is at least none");
  Expects(value.auth <= SDLRDP_AUTH_NLA, "authentication mode is at most NLA");
  _config.user = value.user ? _user.c_str() : nullptr;
  _config.password = value.password ? _password.c_str() : nullptr;
  _config.domain = value.domain ? _domain.c_str() : nullptr;
}
Authentication::~Authentication() {
  OPENSSL_cleanse(_password.data(), _password.size());
}
sdlrdp_config const& Authentication::Config() const noexcept {
  return _config;
}
}
