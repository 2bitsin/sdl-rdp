#pragma once
#include <cstdint>
#include <sys/types.h>

namespace SampleGate {
auto ProcfsSelf()                 -> pid_t;
auto ListeningPort(pid_t pid = 0) -> std::uint32_t;
}
