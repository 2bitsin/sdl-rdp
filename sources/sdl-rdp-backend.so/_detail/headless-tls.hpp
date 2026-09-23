#pragma once
#include "contract.hpp"

#include <arpa/inet.h>
#include <array>
#include <memory>
#include <openssl/ssl.h>
#include <unistd.h>

namespace Headless {
struct TlsSocket {
public:
  TlsSocket(TlsSocket const&) = delete;
  TlsSocket(TlsSocket&&)      = delete;
  TlsSocket()                 = default;
  ~TlsSocket() {
    if (descriptor >= 0) close(descriptor);
  }
  TlsSocket& operator =(TlsSocket const&) = delete;
  TlsSocket& operator =(TlsSocket&&) = delete;
  int Get() const { return descriptor; }
  void Release() { descriptor = -1; }

private:
  int descriptor = socket(AF_INET, SOCK_STREAM, 0);
};

inline void NegotiateTls(int descriptor, unsigned port) {
  using utilities::Expects;
  sockaddr_in address{ };
  address.sin_family      = AF_INET;
  address.sin_port        = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  Expects(descriptor >= 0, "socket was opened");
  if (descriptor < 0) throw std::runtime_error("TLS socket could not be opened");
  Expects(connect(descriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
          "TLS socket connects to the listener");
  std::array<unsigned char, 19> negotiation{ 3, 0, 0, 19, 14, 224, 0, 0, 0, 0, 0, 1, 0, 8, 0, 1, 0, 0, 0 };
  Expects(send(descriptor, negotiation.data(), negotiation.size(), 0) == 19, "RDP negotiation request is sent");
  Expects(recv(descriptor, negotiation.data(), negotiation.size(), MSG_WAITALL) == 19,
          "RDP negotiation response is complete");
}

// FreeRDP 3.15 publishes its lazy BIO method before initialising it; a raw
// OpenSSL handshake primes the server before an in-process client races it.
inline void InitializeTls(unsigned port) {
  using utilities::Expects;
  TlsSocket const socket;
  NegotiateTls(socket.Get(), port);
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> const context(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
  Expects(context != nullptr, "TLS context allocated");
  std::unique_ptr<SSL, decltype(&SSL_free)> const tls(SSL_new(context.get()), SSL_free);
  Expects(tls != nullptr, "TLS session allocated");
  SSL_set_verify(tls.get(), SSL_VERIFY_NONE, nullptr);
  Expects(SSL_set_fd(tls.get(), socket.Get()) == 1, "TLS uses the connected socket");
  Expects(SSL_connect(tls.get()) == 1, "TLS handshake succeeds");
  SSL_shutdown(tls.get());
}
}
