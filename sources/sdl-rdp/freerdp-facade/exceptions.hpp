#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>

namespace Backend::detail::exceptions {
using EventWaitFailed = RuntimeFailure<"EventWaitFailed"_hash, "Event readiness wait failed.">;
}
namespace Backend {
using detail::exceptions::EventWaitFailed;
}
