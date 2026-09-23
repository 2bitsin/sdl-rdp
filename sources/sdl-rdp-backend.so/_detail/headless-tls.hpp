#pragma once
#include "contract.hpp"
#include <array>
#include <memory>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/ssl.h>

namespace Headless {
// FreeRDP 3.15 publishes its lazy BIO method before initialising it; a raw
// OpenSSL handshake primes the server before an in-process client races it.
inline void InitializeTls(unsigned port)
{
  using utilities::Expects;
  struct Socket {
  public:
    Socket(Socket const&)            = delete;
    Socket& operator=(Socket const&) = delete;
    Socket(Socket&&)                 = delete;
    Socket& operator=(Socket&&)      = delete;
    Socket()                         = default;
    ~Socket()
    {
      if (descriptor >= 0) close(descriptor);
    }
    int descriptor = socket(AF_INET, SOCK_STREAM, 0);
  };
  Socket const socket;
  sockaddr_in  address{ };
  address.sin_family      = AF_INET;
  address.sin_port        = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  Expects(socket.descriptor >= 0 && connect(socket.descriptor,
                                            reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
          "TLS socket connects");
  std::array<unsigned char, 19> negotiation{ 3, 0, 0, 19, 14, 224, 0, 0, 0, 0, 0, 1, 0, 8, 0, 1, 0, 0, 0 };
  Expects(send(socket.descriptor, negotiation.data(), negotiation.size(), 0) == 19 && recv(socket.descriptor, negotiation.data(), negotiation.size(), MSG_WAITALL) == 19,
          "RDP TLS negotiation completes");
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> const context(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
  Expects(context != nullptr, "TLS context allocated");
  std::unique_ptr<SSL, decltype(&SSL_free)> const tls(SSL_new(context.get()), SSL_free);
  Expects(tls != nullptr, "TLS session allocated");
  SSL_set_verify(tls.get(), SSL_VERIFY_NONE, nullptr);
  Expects(SSL_set_fd(tls.get(), socket.descriptor) == 1 && SSL_connect(tls.get()) == 1,
          "TLS initialization handshake completes");
  SSL_shutdown(tls.get());
}
}
