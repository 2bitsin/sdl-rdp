#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sdl_rdp::backend::detail::exceptions {
using ::Backend::operator""_hash;
using ::Backend::ArgumentFailure;
using ::Backend::RuntimeFailure;
using std::string_view;

using InvalidChoice     = ArgumentFailure<"InvalidChoice"_hash, "Invalid {}.", string_view>;
using DamageOutOfBounds = ArgumentFailure<"DamageOutOfBounds"_hash, "Present failed: damage exceeds the frame.">;
using NoDriveChannel    = RuntimeFailure<"NoDriveChannel"_hash, "Drive peer disconnected or no drives shared.">;

using OutOfRange = ArgumentFailure<"OutOfRange"_hash, "{} {} is outside {}..{}.", string_view, std::uint32_t,
                                   std::uint32_t, std::uint32_t>;

using InvalidArguments = ArgumentFailure<"InvalidArguments"_hash, "Invalid {} arguments: {}.", string_view,
                                         string_view>;

using EntryNameTooLong = RuntimeFailure<
    "EntryNameTooLong"_hash, "Drive entry name of {} bytes exceeds ABI capacity {}.", std::size_t, std::size_t>;
}
namespace sdl_rdp::backend {
using detail::exceptions::DamageOutOfBounds;
using detail::exceptions::EntryNameTooLong;
using detail::exceptions::InvalidArguments;
using detail::exceptions::InvalidChoice;
using detail::exceptions::NoDriveChannel;
using detail::exceptions::OutOfRange;
}
