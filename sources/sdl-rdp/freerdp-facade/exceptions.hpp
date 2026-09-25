#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <string_view>

namespace Backend::detail::exceptions {
using EventWaitFailed = RuntimeFailure<"EventWaitFailed"_hash, "Event readiness wait failed.">;
using NtlmHashFailed  = RuntimeFailure<"NtlmHashFailed"_hash, "NTLM {} hash failed.", std::string_view>;
}
namespace Backend {
using detail::exceptions::EventWaitFailed;
using detail::exceptions::NtlmHashFailed;
}
