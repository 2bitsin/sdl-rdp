#pragma once
#include <functional>

namespace Backend {
template <class Made, class... Inputs> using Factory = std::move_only_function<Made(Inputs...)>;
}
