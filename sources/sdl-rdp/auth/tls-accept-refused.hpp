#pragma once
#include <stdexcept>

namespace Backend {
class TlsAcceptRefused : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};
}
