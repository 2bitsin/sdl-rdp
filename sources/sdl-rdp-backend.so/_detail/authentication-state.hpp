#pragma once
#include <string>

namespace Backend {
class AuthenticationState {
public:
  auto Identify(std::string user_name, std::string domain_name) -> void;
  auto User() const noexcept                                    -> std::string const&;
  auto Domain() const noexcept                                  -> std::string const&;
  auto TestAndSetChecked() noexcept                             -> bool;
  auto TestAndSetRejected() noexcept                            -> bool;
  auto Rejected() const noexcept                                -> bool;
  auto AttemptHash() noexcept                                   -> void;
  auto Abandoned() const noexcept                               -> bool;

private:
  std::string _user;
  std::string _domain;
  bool        _checked       { };
  bool        _rejected      { };
  bool        _hash_attempted{ };
};
}
