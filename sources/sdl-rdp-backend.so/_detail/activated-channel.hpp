#pragma once
#include "dynamic-channel.hpp"

#include <functional>

namespace Backend {
class ActivatedChannel final : public DynamicChannel {
public:
  explicit ActivatedChannel(std::move_only_function<auto()->bool> activate) noexcept;
  auto     Activate() -> bool                                               override;
  auto     Reject()   -> void                                               override;

private:
  std::move_only_function<auto()->bool> _activate;
};
}
