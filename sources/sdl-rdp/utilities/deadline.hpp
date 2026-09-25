#pragma once
#include <chrono>
#include <concepts>
#include <functional>
#include <thread>
#include <utility>

namespace sdl_rdp::utilities::detail::deadline {
using Deadline = std::chrono::steady_clock::time_point;
auto DeadlineAfter(std::chrono::milliseconds timeout) -> Deadline;
auto AbiDeadline(int timeout_ms)                      -> Deadline;

// Runs `wait` between checks of `ready` until it holds or the deadline passes; a failed wait ends it early.
template <std::predicate WaitTy, std::predicate ReadyTy>
auto Until(Deadline deadline, WaitTy wait, ReadyTy ready) -> bool {
  while (!std::invoke(ready) && std::chrono::steady_clock::now() < deadline)
    if (!std::invoke(wait)) return false;
  return std::invoke(ready);
}
template <std::predicate StepTy>
auto Throughout(Deadline deadline, StepTy step) -> bool {
  return Until(deadline, std::move(step), [deadline] { return std::chrono::steady_clock::now() >= deadline; });
}
template <typename RepTy, typename PeriodTy>
auto Sleeping(std::chrono::duration<RepTy, PeriodTy> interval) -> std::predicate auto {
  return [interval] {
    std::this_thread::sleep_for(interval);
    return true;
  };
}
}

namespace sdl_rdp::utilities {
using detail::deadline::AbiDeadline;
using detail::deadline::Deadline;
using detail::deadline::DeadlineAfter;
using detail::deadline::Sleeping;
using detail::deadline::Throughout;
using detail::deadline::Until;
}
