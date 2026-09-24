#pragma once

namespace Backend {
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
}
