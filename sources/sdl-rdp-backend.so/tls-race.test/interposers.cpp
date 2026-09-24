#include "_detail/contract.hpp"
#include "tls-race.test/injected-faults.hpp"
#include "tls-race.test/method-fill.hpp"

#include <openssl/ssl.h>
#include <dlfcn.h>

namespace {
using Write = auto (*)(BIO*, char const*, int) -> int;
constexpr int SslFailure = -1;
constexpr int SslRefused = 0;

template <typename VFunction> auto Next(char const* name) -> VFunction* {
  auto* const found = dlsym(RTLD_NEXT, name);
  utilities::Expects(found != nullptr, "the next library defines the interposed symbol");
  return reinterpret_cast<VFunction*>(found);
}
auto Swallowed([[maybe_unused]] BIO* bio, [[maybe_unused]] char const* data, int size) -> int {
  return size;
}
auto WriteFor(BIO_METHOD const* method, Write write) -> Write {
  auto const dropped = Race::InjectedFaults::Shared().ServerWritesDropped();
  return dropped && Race::MethodFill::Shared().Named(method, Race::SocketMethod) ? Swallowed : write;
}
}
extern "C" [[gnu::visibility("default")]] auto BIO_meth_new(int type, char const* name) -> BIO_METHOD* {
  static auto* const next   = Next<decltype(BIO_meth_new)>("BIO_meth_new");
  auto* const        method = next(type, name);
  if (method != nullptr && name != nullptr) Race::MethodFill::Shared().Created(method, name);
  return method;
}
extern "C" [[gnu::visibility("default")]] auto BIO_meth_set_write(BIO_METHOD* biom, Write write) -> int {
  static auto* const next = Next<decltype(BIO_meth_set_write)>("BIO_meth_set_write");
  Race::MethodFill::Shared().Filling(biom, Race::Setter::Write);
  return next(biom, WriteFor(biom, write));
}
extern "C" [[gnu::visibility("default")]] auto BIO_meth_set_create(BIO_METHOD* biom, auto (*create)(BIO*)->int) -> int {
  static auto* const next = Next<decltype(BIO_meth_set_create)>("BIO_meth_set_create");
  Race::MethodFill::Shared().Filling(biom, Race::Setter::Create);
  return next(biom, create);
}
extern "C" [[gnu::visibility("default")]] auto SSL_use_PrivateKey(SSL* ssl, EVP_PKEY* pkey) -> int {
  static auto* const next = Next<decltype(SSL_use_PrivateKey)>("SSL_use_PrivateKey");
  return Race::InjectedFaults::Shared().PrivateKeyRefused() ? SslRefused : next(ssl, pkey);
}
extern "C" [[gnu::visibility("default")]] auto SSL_connect(SSL* ssl) -> int {
  static auto* const next = Next<decltype(SSL_connect)>("SSL_connect");
  return Race::InjectedFaults::Shared().ClientSilenced() ? SslFailure : next(ssl);
}
