#pragma once
#include <sdl-rdp/SDL3/rdp/driver.hpp>
#include <concepts>
#include <memory>
namespace sdl3::rdp::detail::owneddriver {
using sdl_rdp::utilities::Expects;

// The closed set of shares: the video data drives the backend, audio and storage only read it.
template <typename DriverTy>
concept DriverShare = std::same_as<DriverTy, Driver> || std::same_as<DriverTy, Driver const>;
template <DriverShare DriverTy>
class OwnedDriver {
public:
  explicit OwnedDriver(std::shared_ptr<DriverTy> driver) : _driver{ std::move(driver) } {
    Expects(_driver != nullptr, "a driver share holds a driver");
  }
  auto Backend() noexcept -> DriverTy& {
    return *_driver;
  }
  auto Backend() const noexcept -> Driver const& {
    return *_driver;
  }
  auto Owner() const noexcept -> std::shared_ptr<DriverTy> const& {
    return _driver;
  }
private:
  std::shared_ptr<DriverTy> _driver;
};
}

namespace sdl3::rdp {
using detail::owneddriver::DriverShare;
using detail::owneddriver::OwnedDriver;
}
