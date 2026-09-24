#pragma once
#include <chrono>

namespace Backend {
using Deadline = std::chrono::steady_clock::time_point;
auto DeadlineAfter(std::chrono::milliseconds timeout) -> Deadline;
auto AbiDeadline(int timeout_ms)                      -> Deadline;
}
