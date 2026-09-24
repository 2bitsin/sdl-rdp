#include <sdl-rdp/auth/auth.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/transcode.hpp>

#include <openssl/crypto.h>
#include <oxbox/utilities/span.hpp>
#include <winpr/ntlm.h>
#include <cstring>

namespace {
auto PairName(sdlrdp_config const& config, char const* domain, char const* user) -> bool {
  utilities::Expects(domain, "credential names exist");
  utilities::Expects(user, "credential names exist");
  return config.password && config.user && std::strcmp(config.user, user) == 0
         && (!config.domain || std::strcmp(config.domain, domain) == 0);
}
}
auto sdlrdp_verify_pair(sdlrdp_config const* config, char const* domain, char const* user, char const* password)
    -> int {
  if (!config || !domain || !user || !password || !PairName(*config, domain, user)) return 0;
  auto length = std::strlen(config->password);
  return length == std::strlen(password) && CRYPTO_memcmp(config->password, password, length) == 0;
}
auto sdlrdp_lookup_pair(sdlrdp_config const* config, char const* domain, char const* user, unsigned char hash[16])
    -> int {
  if (!config || !domain || !user || !hash || !PairName(*config, domain, user)) return 0;
  try {
    auto bytes  = Backend::TranscodeRange<std::vector<BYTE>>(
        std::as_bytes(std::span(config->password, std::strlen(config->password))), { }, Backend::Utf16Little);
    auto length = bytes.size();
    bytes.resize(length + sizeof(WCHAR));
    auto result = NTOWFv1W(oxbox::utilities::SpanCast<uint16_t>(std::span(bytes)).data(), length, hash);
    OPENSSL_cleanse(bytes.data(), bytes.size());
    return result;
  } catch (...) {
    return 0;
  }
}
