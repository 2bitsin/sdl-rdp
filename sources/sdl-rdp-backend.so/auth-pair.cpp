#include "_detail/auth.hpp"
#include "_detail/contract.hpp"
#include "_detail/transcode.hpp"

#include <cstring>
#include <openssl/crypto.h>
#include <winpr/ntlm.h>

namespace Backend {
Authentication::Authentication(sdlrdp_config const& value)
    : config(value), user(value.user ? value.user : ""), password(value.password ? value.password : ""),
      domain(value.domain ? value.domain : "") {
  utilities::Expects(value.auth >= SDLRDP_AUTH_NONE, "valid authentication mode");
  utilities::Expects(value.auth <= SDLRDP_AUTH_NLA, "valid authentication mode");
  config.user     = value.user ? user.c_str() : nullptr;
  config.password = value.password ? password.c_str() : nullptr;
  config.domain   = value.domain ? domain.c_str() : nullptr;
}
Authentication::~Authentication() {
  OPENSSL_cleanse(password.data(), password.size());
}
}
namespace {
bool PairName(sdlrdp_config const& config, char const* domain, char const* user) {
  utilities::Expects(domain, "credential names exist");
  utilities::Expects(user, "credential names exist");
  return config.password && config.user && std::strcmp(config.user, user) == 0 &&
         (!config.domain || std::strcmp(config.domain, domain) == 0);
}
}
int sdlrdp_verify_pair(sdlrdp_config const* config, char const* domain, char const* user, char const* password) {
  if (!config || !domain || !user || !password || !PairName(*config, domain, user)) return 0;
  auto length = std::strlen(config->password);
  return length == std::strlen(password) && CRYPTO_memcmp(config->password, password, length) == 0;
}
int sdlrdp_lookup_pair(sdlrdp_config const* config, char const* domain, char const* user, unsigned char hash[16]) {
  if (!config || !domain || !user || !hash || !PairName(*config, domain, user)) return 0;
  try {
    auto bytes = Backend::TranscodeRange<std::vector<BYTE>>(
        std::as_bytes(std::span(config->password, std::strlen(config->password))), {},
        { .encoding = oxbox::utilities::Encoding::UTF16, .order = std::endian::little });
    auto length = bytes.size();
    bytes.resize(length + sizeof(WCHAR));
    auto result = NTOWFv1W(reinterpret_cast<WCHAR*>(bytes.data()), length, hash);
    OPENSSL_cleanse(bytes.data(), bytes.size());
    return result;
  } catch (...) {
    return 0;
  }
}
