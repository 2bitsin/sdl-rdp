#pragma once
#include <functional>
#include <sys/types.h>

namespace Headless {
class ChildProcess {
public:
  explicit           ChildProcess(std::function<int()> const& body);
                     ChildProcess(ChildProcess&& other)                noexcept;
                     ChildProcess(ChildProcess const&)                 = delete;
                     ~ChildProcess();
  auto               operator = (ChildProcess&&)      -> ChildProcess& = delete;
  auto               operator = (ChildProcess const&) -> ChildProcess& = delete;
  [[nodiscard]] auto Wait()                           -> int;
  [[nodiscard]] auto ExitedCleanly()                  -> bool;
  auto               Kill() const noexcept            -> void;

private:
  static constexpr pid_t Reaped = 0;
  pid_t                  pid;
};
}
