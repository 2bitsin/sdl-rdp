#pragma once
#include <sdl-rdp/utilities/nt-owf.hpp>

#include <string_view>

namespace sdl_rdp::freerdp_facade::detail::ntlm {
using sdl_rdp::utilities::NtOwf;

auto NtOwfV1(std::u16string_view password)                                          -> NtOwf;
auto NtOwfV2(NtOwf const& v1, std::u16string_view user, std::u16string_view domain) -> NtOwf;
}

namespace sdl_rdp::freerdp_facade {
using detail::ntlm::NtOwfV1;
using detail::ntlm::NtOwfV2;
}
