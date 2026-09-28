#pragma once
#include <string_view>

namespace sdl_rdp::utilities::detail::descriptor {
// A POSIX file descriptor, closed by its destructor; the Windows build has no such handle.
class Descriptor {
public:
  explicit           Descriptor(int owned)                                 noexcept;
                     Descriptor(Descriptor&& other)                        noexcept;
                     Descriptor(Descriptor const&)                         = delete;
                     ~Descriptor();
  auto               operator=(Descriptor&& other) noexcept -> Descriptor&;
  auto               operator=(Descriptor const&)           -> Descriptor& = delete;
  [[nodiscard]] auto Owns() const noexcept                  -> bool;
  [[nodiscard]] auto Get() const noexcept                   -> int;
  [[nodiscard]] auto Release() noexcept                     -> int;

private:
  static constexpr int Closed     = -1;
  int                  descriptor;
};
auto SystemCall(int result, std::string_view operation) -> int;
}

namespace sdl_rdp::utilities {
using detail::descriptor::Descriptor;
using detail::descriptor::SystemCall;
}
