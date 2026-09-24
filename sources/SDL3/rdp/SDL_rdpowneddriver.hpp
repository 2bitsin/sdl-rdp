#pragma once
#include "SDL_rdpdriver.hpp"
#include <concepts>
#include <memory>
namespace rdp {
// The closed set of shares: the video data drives the backend, audio and storage only read it.
template <typename _Driver>
concept DriverShare = std::same_as<_Driver, Driver> || std::same_as<_Driver, Driver const>;
template <DriverShare _Driver>
class OwnedDriver {
public:
  explicit OwnedDriver(std::shared_ptr<_Driver> driver) : _driver{ std::move(driver) } {
    utilities::Expects(_driver != nullptr, "a driver share holds a driver");
  }
  auto Backend() noexcept -> _Driver& {
    return *_driver;
  }
  auto Backend() const noexcept -> Driver const& {
    return *_driver;
  }
  auto Owner() const noexcept -> std::shared_ptr<_Driver> const& {
    return _driver;
  }
private:
  std::shared_ptr<_Driver> _driver;
};
}
