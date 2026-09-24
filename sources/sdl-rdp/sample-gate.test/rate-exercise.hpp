#pragma once
#include <sdl-rdp/core/refresh.hpp>
#include <sdl-rdp/headless-client.test/logs.hpp>
namespace SampleGate {
enum class RateRecovery{ InPlace, AfterResize };
auto ExerciseRate(unsigned port, Headless::Logs& logs, Backend::RefreshMode mode, RateRecovery recovery) -> void;
}
