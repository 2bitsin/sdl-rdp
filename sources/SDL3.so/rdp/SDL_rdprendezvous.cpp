#include "SDL_rdprendezvous.hpp"
namespace rdp {
namespace {
constexpr auto RendezvousProperty = "SDL.rdp.internal.driver";
}
auto SDLCALL Rendezvous::_Cleanup([[maybe_unused]] void* unused, void* value) -> void {
  utilities::Expects(value != nullptr, "rendezvous property owns its value");
  std::unique_ptr<Rendezvous> const owner{ static_cast<Rendezvous*>(value) };
}
auto Rendezvous::_Published() -> Rendezvous& {
  auto const properties = SDL_GetGlobalProperties();
  if (!properties) throw std::runtime_error(SDL_GetError());
  ScopedPropertiesLock const lock{ properties };
  if (!SDL_GetPointerProperty(properties, RendezvousProperty, nullptr)) {
    auto owner = std::make_unique<Rendezvous>();
    // SDL owns the published value and hands it back to _Cleanup, also when publishing fails.
    if (!SDL_SetPointerPropertyWithCleanup(properties, RendezvousProperty, owner.release(), _Cleanup, nullptr))
      throw std::runtime_error(SDL_GetError());
  }
  return *static_cast<Rendezvous*>(SDL_GetPointerProperty(properties, RendezvousProperty, nullptr));
}
auto Rendezvous::_Driver() -> std::shared_ptr<Driver> {
  std::scoped_lock const lock   { _mutex };
  auto                   driver = _driver.lock();
  if (driver) return driver;
  driver  = std::make_shared<Driver>();
  _driver = driver;
  return driver;
}
auto Rendezvous::Acquire() -> std::shared_ptr<Driver> { return _Published()._Driver(); }
}
