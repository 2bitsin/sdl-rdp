#include <sdl-rdp/auth/auth.hpp>
#include <sdl-rdp/session/handle.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/transcode.hpp>

#include <openssl/crypto.h>
#include <oxbox/utilities/span.hpp>
#include <winpr/ntlm.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

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
static_assert(std::is_same_v<decltype(&sdlrdp_lookup_pair),
                             auto (*)(sdlrdp_config const*, char const*, char const*, std::uint8_t*)->int>,
              "the ABI's unsigned char hash[16] is the std::uint8_t* defined here");
auto sdlrdp_lookup_pair(sdlrdp_config const* config, char const* domain, char const* user, std::uint8_t hash[16])
    -> int {
  if (!config || !domain || !user || !hash || !PairName(*config, domain, user)) return 0;
  auto const hashed = [&] {
    auto bytes  = Backend::TranscodeRange<std::vector<std::uint8_t>>(
        std::as_bytes(std::span(config->password, std::strlen(config->password))), { }, Backend::Utf16Little);
    auto length = bytes.size();
    bytes.resize(length + sizeof(char16_t));
    auto result = NTOWFv1W(oxbox::utilities::SpanCast<std::uint16_t>(std::span(bytes)).data(),
                           Backend::Narrowed<std::uint32_t>(length), hash);
    OPENSSL_cleanse(bytes.data(), bytes.size());
    return int{ result };
  };
  return Backend::Contained(0, hashed, [](std::string_view text) { Backend::SetError(nullptr, std::string{ text }); });
}
