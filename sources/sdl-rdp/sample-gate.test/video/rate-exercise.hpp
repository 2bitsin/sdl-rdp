#pragma once
#include <sdl-rdp/configuration/refresh.hpp>
#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <cstdint>
namespace sdl_rdp::sample_gate_test::video::detail::rate_exercise {
using sdl_rdp::configuration::RefreshMode;
using sdl_rdp::headless_client_test::backend::Logs;

enum class RateRecovery{ InPlace, AfterResize };
auto ExerciseRate(std::uint32_t port, Logs& logs, RefreshMode mode, RateRecovery recovery) -> void;
}

namespace sdl_rdp::sample_gate_test::video {
using detail::rate_exercise::ExerciseRate;
using detail::rate_exercise::RateRecovery;
}
