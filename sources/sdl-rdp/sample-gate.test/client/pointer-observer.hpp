#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>

namespace SampleGate {
struct PointerObserver {
public:
  explicit PointerObserver(Headless::Client& client);
  auto     Red() const -> bool;

private:
  auto Receive(POINTER_NEW_UPDATE const& update) -> void;
  inline static thread_local PointerObserver* active = nullptr;
  bool                                        red    = false;
};
}
