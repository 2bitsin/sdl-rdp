#include "rendezvous.hpp"
#include "exceptions.hpp"
#include <sdl-rdp/SDL3/rdp/backend/resources.hpp>
#include <memory>
#include <stdexcept>
namespace sdl3::rdp::detail::rendezvous {
using backend::ScopedPropertiesLock;
namespace {
constexpr auto RendezvousProperty = "SDL.rdp.internal.driver";
}
auto SDLCALL Rendezvous::Cleanup([[maybe_unused]] void* unused, void* value) -> void {
  utilities::Expects(value != nullptr, "rendezvous property owns its value");
  std::unique_ptr<Rendezvous> const owner{ static_cast<Rendezvous*>(value) };
}
auto Rendezvous::Published() -> Rendezvous& {
  auto const properties = SDL_GetGlobalProperties();
  if (!properties) throw RelayedFailure{ SDL_GetError() };
  ScopedPropertiesLock const lock{ properties };
  if (!SDL_GetPointerProperty(properties, RendezvousProperty, nullptr)) {
    auto owner = std::make_unique<Rendezvous>();
    // SDL owns the published value and hands it back to Cleanup, also when publishing fails.
    if (!SDL_SetPointerPropertyWithCleanup(properties, RendezvousProperty, owner.release(), Cleanup, nullptr))
      throw RelayedFailure{ SDL_GetError() };
  }
  return *static_cast<Rendezvous*>(SDL_GetPointerProperty(properties, RendezvousProperty, nullptr));
}
auto Rendezvous::SharedDriver() -> std::shared_ptr<Driver> {
  std::scoped_lock const lock   { _mutex };
  auto                   driver = _driver.lock();
  if (driver) return driver;
  driver  = std::make_shared<Driver>();
  _driver = driver;
  return driver;
}
auto Rendezvous::Acquire() -> std::shared_ptr<Driver> {
  return Published().SharedDriver();
}
}
