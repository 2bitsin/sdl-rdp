#pragma once
#include <sdl-rdp/utilities/scoped.hpp>

#include <atomic>
#include <cstdint>
#include <utility>

namespace sdl_rdp::integration::allocations_test::detail::counting_heap {
using sdl_rdp::utilities::RAIIWrap;

struct Tally {
  std::uint64_t news = 0;
  std::uint64_t heap = 0;
};

class CountingHeap {
public:
  // The replaced allocation functions take no user pointer, so their tally is one object per process.
  static auto        Shared() noexcept               -> CountingHeap&;
  static auto        Suspend() noexcept              -> bool;
  static auto        Restore(bool previous) noexcept -> void;
  auto               CountNew() noexcept             -> void;
  auto               CountHeap() noexcept            -> void;
  [[nodiscard]] auto Current() const noexcept        -> Tally;

private:
  static thread_local bool   suspended;
  std::atomic<std::uint64_t> news     { 0 };
  std::atomic<std::uint64_t> heap     { 0 };
};

using Uncounted = RAIIWrap<bool, &CountingHeap::Suspend, &CountingHeap::Restore>;
}

namespace sdl_rdp::integration::allocations_test {
using detail::counting_heap::CountingHeap;
using detail::counting_heap::Tally;
using detail::counting_heap::Uncounted;
}
