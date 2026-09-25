#include <sdl-rdp/drive/information.hpp>

#include <sdl-rdp/freerdp-facade/rdpdr.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <chrono>
#include <cstddef>
#include <utility>

namespace sdl_rdp::drive::detail::information {
using sdl_rdp::freerdp_facade::FileAttribute;
using sdl_rdp::utilities::Narrowed;

auto InformationRequest(InformationClass type, DrivePacket const& body) -> DrivePacket {
  constexpr std::size_t padding_after_length = 24;
  DrivePacket           packet;
  packet.Write(std::to_underlying(type));
  packet.Write(Narrowed<std::uint32_t>(body.Bytes().size()));
  packet.Zero(padding_after_length);
  packet.Append(body.Bytes());
  return packet;
}
auto Information(DrivePacket response) -> DrivePacket {
  auto const length = response.Read<std::uint32_t>();
  if (length > response.Bytes().size() - response.Position()) response.Invalid("truncated information");
  return response;
}
// MS-FSCC 2.4.7 FILE_BASIC_INFORMATION.
auto Basic(DrivePacket basic) -> BasicInformation {
  constexpr std::size_t last_write_time_offset = 16;
  constexpr std::size_t change_time_size       = 8;
  basic.Skip(last_write_time_offset);
  auto const modified = basic.Read<std::uint64_t>();
  basic.Skip(change_time_size);
  auto const attributes = basic.Read<std::uint32_t>();
  return { .directory = (attributes & std::to_underlying(FileAttribute::Directory)) != 0,
           .modified  = UnixSeconds(modified) };
}
// MS-FSCC 2.4.41 FILE_STANDARD_INFORMATION.
auto EndOfFile(DrivePacket standard) -> std::uint64_t {
  constexpr std::size_t end_of_file_offset = 8;
  standard.Skip(end_of_file_offset);
  return standard.Read<std::uint64_t>();
}
auto UnixSeconds(std::uint64_t filetime) -> std::int64_t {
  // WinPR 3.32 timezone.c:689 FileTimeToSystemTime is a stub on Linux.
  constexpr std::uint64_t filetime_ticks_per_second = 10'000'000;
  using namespace std::chrono;
  constexpr auto epoch = duration_cast<seconds>(sys_days{ 1970y / January / 1 } - sys_days{ 1601y / January / 1 })
                             .count();
  return Narrowed<std::int64_t>(filetime / filetime_ticks_per_second) - epoch;
}
}
