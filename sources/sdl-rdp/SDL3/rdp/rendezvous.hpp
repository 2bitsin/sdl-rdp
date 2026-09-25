#pragma once
#include <sdl-rdp/SDL3/rdp/driver.hpp>
#include <memory>
#include <mutex>
namespace sdl3::rdp::detail::rendezvous {
// SDL opens subsystems without shared context; its global properties supply the ABI rendezvous.
class Rendezvous {
public:
  static auto Acquire() -> std::shared_ptr<Driver>;
private:
  // SDL property destruction passes the stored pointer and an opaque context.
  static auto SDLCALL _Cleanup(void* unused, void* value) -> void;
  static auto         _Published()                        -> Rendezvous&;
  auto                _Driver()                           -> std::shared_ptr<Driver>;
  std::mutex            _mutex;
  std::weak_ptr<Driver> _driver;
};
}
namespace sdl3::rdp {
using detail::rendezvous::Rendezvous;
}
