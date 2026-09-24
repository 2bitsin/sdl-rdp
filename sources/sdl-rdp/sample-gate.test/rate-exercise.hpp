#pragma once
#include <sdl-rdp/core/refresh.hpp>
#include <sdl-rdp/headless-client.test/logs.hpp>
#include <cstdint>
namespace SampleGate {
enum class RateRecovery{ InPlace, AfterResize };
auto ExerciseRate(std::uint32_t port, Headless::Logs& logs, Backend::RefreshMode mode, RateRecovery recovery) -> void;
}
