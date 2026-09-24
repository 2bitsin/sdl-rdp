#pragma once
#include <string>

namespace Backend {
class AuthenticationState {
public:
  void               Identify(std::string user_name, std::string domain_name);
  std::string const& User() const         noexcept;
  std::string const& Domain() const       noexcept;
  bool               TestAndSetChecked()  noexcept;
  bool               TestAndSetRejected() noexcept;
  bool               Rejected() const     noexcept;
  void               AttemptHash()        noexcept;
  bool               Abandoned() const    noexcept;

private:
  std::string _user;
  std::string _domain;
  bool        _checked       { };
  bool        _rejected      { };
  bool        _hash_attempted{ };
};
}
