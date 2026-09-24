#pragma once
#include <sdl-rdp-backend.so/_detail/client.hpp>

namespace SampleGate {
struct PointerObserver {
public:
  explicit PointerObserver(Headless::Client& client);
  auto     Red() const -> bool;

private:
  static auto Receive(rdpContext* /*unused*/, POINTER_NEW_UPDATE const* update) -> BOOL;
  inline static thread_local PointerObserver* active = nullptr;
  bool                                        red    = false;
};
}
