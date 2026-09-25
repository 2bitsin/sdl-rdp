#pragma once
#include <functional>

namespace sdl_rdp::utilities::detail::factory {
template <class Made, class... Inputs> using Factory = std::move_only_function<Made(Inputs...)>;
}

namespace sdl_rdp::utilities {
using detail::factory::Factory;
}
