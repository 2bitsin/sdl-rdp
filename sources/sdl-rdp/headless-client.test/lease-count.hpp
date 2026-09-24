#pragma once
#include <sdl-rdp/utilities/pinned.hpp>

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace Headless {
class LeaseCount : private Backend::Pinned {
public:
       LeaseCount() = default;
  auto Acquire()          -> void;
  auto Release() noexcept -> void;
  auto Drain()            -> void;

private:
  std::mutex              _guard;
  std::condition_variable _released;
  std::size_t             _leases  { };
};
}
