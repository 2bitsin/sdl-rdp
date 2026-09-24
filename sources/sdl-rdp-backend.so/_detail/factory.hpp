#pragma once
#include <functional>

namespace Backend {
template <class Made> using Factory = std::move_only_function<Made()>;
}
