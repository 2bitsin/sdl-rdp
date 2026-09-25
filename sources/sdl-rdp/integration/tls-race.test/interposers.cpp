#include "injected-faults.hpp"
#include "method-fill.hpp"
#include <sdl-rdp/utilities/contract.hpp>

#include <openssl/ssl.h>
#include <dlfcn.h>
#include <string>
#include <type_traits>

namespace sdl_rdp::integration::tls_race_test::detail::interposers {
using sdl_rdp::utilities::Expects;
namespace {
using Write = auto (*)(BIO*, char const*, int) -> int;
constexpr int SslFailure = -1;
constexpr int SslRefused = 0;

template <auto& interposed> auto Next(std::string const& name) -> decltype(interposed) {
  auto* const found = dlsym(RTLD_NEXT, name.c_str());
  Expects(found != nullptr, "the next library defines the interposed symbol");
  // dlsym(3) hands every symbol back as void*.
  return *reinterpret_cast<std::remove_reference_t<decltype(interposed)>*>(found);
}
auto Swallowed([[maybe_unused]] BIO* bio, [[maybe_unused]] char const* data, int size) -> int {
  return size;
}
auto WritesDropped(BIO_METHOD const& method) -> bool {
  return InjectedFaults::Shared().ServerWritesDropped() && MethodFill::Shared().Named(method, SocketMethod);
}
}
extern "C" [[gnu::visibility("default")]] auto BIO_meth_new(int type, char const* name) -> BIO_METHOD* {
  static decltype(BIO_meth_new)& next   = Next<BIO_meth_new>("BIO_meth_new");
  auto* const                    method = next(type, name);
  if (method != nullptr && name != nullptr) MethodFill::Shared().Created(*method, name);
  return method;
}
extern "C" [[gnu::visibility("default")]] auto BIO_meth_set_write(BIO_METHOD* biom, Write write) -> int {
  static decltype(BIO_meth_set_write)& next = Next<BIO_meth_set_write>("BIO_meth_set_write");
  Expects(biom != nullptr, "a method is being filled");
  MethodFill::Shared().Filling(*biom, Setter::Write);
  return next(biom, WritesDropped(*biom) ? Swallowed : write);
}
extern "C" [[gnu::visibility("default")]] auto BIO_meth_set_create(BIO_METHOD* biom, auto (*create)(BIO*)->int) -> int {
  static decltype(BIO_meth_set_create)& next = Next<BIO_meth_set_create>("BIO_meth_set_create");
  Expects(biom != nullptr, "a method is being filled");
  MethodFill::Shared().Filling(*biom, Setter::Create);
  return next(biom, create);
}
extern "C" [[gnu::visibility("default")]] auto SSL_use_PrivateKey(SSL* ssl, EVP_PKEY* pkey) -> int {
  static decltype(SSL_use_PrivateKey)& next = Next<SSL_use_PrivateKey>("SSL_use_PrivateKey");
  if (ssl == nullptr || pkey == nullptr) return next(ssl, pkey);
  return InjectedFaults::Shared().PrivateKeyRefused() ? SslRefused : next(ssl, pkey);
}
extern "C" [[gnu::visibility("default")]] auto SSL_connect(SSL* ssl) -> int {
  static decltype(SSL_connect)& next = Next<SSL_connect>("SSL_connect");
  if (ssl == nullptr) return next(ssl);
  return InjectedFaults::Shared().ClientSilenced() ? SslFailure : next(ssl);
}
}
