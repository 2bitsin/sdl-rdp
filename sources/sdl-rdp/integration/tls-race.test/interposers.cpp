#include "injected-faults.hpp"
#include "method-fill.hpp"
#include <sdl-rdp/utilities/contract.hpp>

#include <openssl/ssl.h>
#include <dlfcn.h>

namespace sdl_rdp::integration::tls_race_test::detail::interposers {
using sdl_rdp::utilities::Expects;
namespace {
using Write = auto (*)(BIO*, char const*, int) -> int;
constexpr int SslFailure = -1;
constexpr int SslRefused = 0;

template <typename VFunction> auto Next(char const* name) -> VFunction* {
  auto* const found = dlsym(RTLD_NEXT, name);
  Expects(found != nullptr, "the next library defines the interposed symbol");
  return reinterpret_cast<VFunction*>(found);
}
auto Swallowed([[maybe_unused]] BIO* bio, [[maybe_unused]] char const* data, int size) -> int {
  return size;
}
auto WriteFor(BIO_METHOD const* method, Write write) -> Write {
  auto const dropped = InjectedFaults::Shared().ServerWritesDropped();
  return dropped && MethodFill::Shared().Named(method, SocketMethod) ? Swallowed : write;
}
}
extern "C" [[gnu::visibility("default")]] auto BIO_meth_new(int type, char const* name) -> BIO_METHOD* {
  static auto* const next   = Next<decltype(BIO_meth_new)>("BIO_meth_new");
  auto* const        method = next(type, name);
  if (method != nullptr && name != nullptr) MethodFill::Shared().Created(method, name);
  return method;
}
extern "C" [[gnu::visibility("default")]] auto BIO_meth_set_write(BIO_METHOD* biom, Write write) -> int {
  static auto* const next = Next<decltype(BIO_meth_set_write)>("BIO_meth_set_write");
  MethodFill::Shared().Filling(biom, Setter::Write);
  return next(biom, WriteFor(biom, write));
}
extern "C" [[gnu::visibility("default")]] auto BIO_meth_set_create(BIO_METHOD* biom, auto (*create)(BIO*)->int) -> int {
  static auto* const next = Next<decltype(BIO_meth_set_create)>("BIO_meth_set_create");
  MethodFill::Shared().Filling(biom, Setter::Create);
  return next(biom, create);
}
extern "C" [[gnu::visibility("default")]] auto SSL_use_PrivateKey(SSL* ssl, EVP_PKEY* pkey) -> int {
  static auto* const next = Next<decltype(SSL_use_PrivateKey)>("SSL_use_PrivateKey");
  return InjectedFaults::Shared().PrivateKeyRefused() ? SslRefused : next(ssl, pkey);
}
extern "C" [[gnu::visibility("default")]] auto SSL_connect(SSL* ssl) -> int {
  static auto* const next = Next<decltype(SSL_connect)>("SSL_connect");
  return InjectedFaults::Shared().ClientSilenced() ? SslFailure : next(ssl);
}
}
