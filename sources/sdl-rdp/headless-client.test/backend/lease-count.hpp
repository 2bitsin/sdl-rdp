#pragma once
#include <sdl-rdp/utilities/pinned.hpp>

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace sdl_rdp::headless_client_test::backend::detail::lease_count {
using sdl_rdp::utilities::Pinned;

class LeaseCount : private Pinned {
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

namespace sdl_rdp::headless_client_test::backend {
using detail::lease_count::LeaseCount;
}
