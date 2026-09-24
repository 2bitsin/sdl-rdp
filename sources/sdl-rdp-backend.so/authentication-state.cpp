#include "_detail/authentication-state.hpp"

#include <utility>

namespace Backend {
auto AuthenticationState::Identify(std::string user_name, std::string domain_name) -> void {
  _user   = std::move(user_name);
  _domain = std::move(domain_name);
}
auto AuthenticationState::User() const noexcept -> std::string const& {
  return _user;
}
auto AuthenticationState::Domain() const noexcept -> std::string const& {
  return _domain;
}
auto AuthenticationState::TestAndSetChecked() noexcept -> bool {
  return std::exchange(_checked, true);
}
auto AuthenticationState::TestAndSetRejected() noexcept -> bool {
  return std::exchange(_rejected, true);
}
auto AuthenticationState::Rejected() const noexcept -> bool {
  return _rejected;
}
auto AuthenticationState::AttemptHash() noexcept -> void {
  _hash_attempted = true;
}
auto AuthenticationState::Abandoned() const noexcept -> bool {
  return _hash_attempted && !_checked;
}
}
