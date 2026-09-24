#pragma once
#include <sdl-rdp-backend.so/_detail/refresh.hpp>
#include <sdl-rdp-backend.so/_detail/test-logs.hpp>
namespace SampleGate {
enum class RateRecovery{ InPlace, AfterResize };
auto ExerciseRate(unsigned port, Headless::Logs& logs, Backend::RefreshMode mode, RateRecovery recovery) -> void;
}
