#include "owneddriver.hpp"
#include <sdl-rdp/utilities/contract.hpp>
#include <utility>
namespace sdl3::rdp::detail::owneddriver {
using sdl_rdp::utilities::Expects;

OwnedDriver::OwnedDriver(std::shared_ptr<sdl3::rdp::Driver> driver) : _driver{ std::move(driver) } {
  Expects(_driver != nullptr, "a driver share holds a driver");
}
auto OwnedDriver::Driver() noexcept -> sdl3::rdp::Driver& {
  return *_driver;
}
auto OwnedDriver::Driver() const noexcept -> sdl3::rdp::Driver const& {
  return *_driver;
}
auto OwnedDriver::Owner() const noexcept -> std::shared_ptr<sdl3::rdp::Driver> const& {
  return _driver;
}
}
