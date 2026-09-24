#include "_detail/authentication-state.hpp"

#include <utility>

namespace Backend {
void AuthenticationState::Identify(std::string user_name, std::string domain_name) {
  _user   = std::move(user_name);
  _domain = std::move(domain_name);
}
std::string const& AuthenticationState::User() const noexcept {
  return _user;
}
std::string const& AuthenticationState::Domain() const noexcept {
  return _domain;
}
bool AuthenticationState::TestAndSetChecked() noexcept {
  return std::exchange(_checked, true);
}
bool AuthenticationState::TestAndSetRejected() noexcept {
  return std::exchange(_rejected, true);
}
bool AuthenticationState::Rejected() const noexcept {
  return _rejected;
}
void AuthenticationState::AttemptHash() noexcept {
  _hash_attempted = true;
}
bool AuthenticationState::Abandoned() const noexcept {
  return _hash_attempted && !_checked;
}
}
