#pragma once
#include <sdl-rdp/SDL3/rdp/driver.hpp>
#include <memory>
namespace sdl3::rdp::detail::owneddriver {
// A subsystem's share of the process's driver: video, audio and storage each hold one.
class OwnedDriver {
public:
  explicit OwnedDriver(std::shared_ptr<sdl3::rdp::Driver> driver);
  auto     Driver() noexcept       -> sdl3::rdp::Driver&;
  auto     Driver() const noexcept -> sdl3::rdp::Driver const&;
  auto     Owner() const noexcept  -> std::shared_ptr<sdl3::rdp::Driver> const&;
private:
  std::shared_ptr<sdl3::rdp::Driver> _driver;
};
}

namespace sdl3::rdp {
using detail::owneddriver::OwnedDriver;
}
