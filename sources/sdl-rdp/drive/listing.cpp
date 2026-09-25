#include <sdl-rdp/drive/listing.hpp>

#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/freerdp-facade/rdpdr.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/terminated-copy.hpp>

#include <string_view>
#include <utility>

namespace sdl_rdp::drive::detail::listing {
using sdl_rdp::freerdp_facade::FileAttribute;
using sdl_rdp::freerdp_facade::InformationClass;
using sdl_rdp::utilities::CopyTerminated;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;

namespace {
auto Listed(sdlrdp_dirent const& entry) -> bool {
  std::string_view const name = entry.name;
  return name != "." && name != "..";
}
}
Listing::Listing(std::uint32_t offset, std::span<sdlrdp_dirent> out) noexcept : _out{ out }, _offset{ offset } { }
// MS-FSCC 2.4.10 FILE_DIRECTORY_INFORMATION entries chained by NextEntryOffset; false once the directory is done.
auto Listing::Collect(DrivePacket response) -> bool {
  Expects(!Full(), "a full listing takes no further response");
  auto const length = response.Read<std::uint32_t>();
  if (length > response.Bytes().size() - response.Position()) response.Invalid("truncated directory listing");
  auto const limit = response.Position() + length;
  while (response.Position() < limit && !Full()) {
    auto const start = response.Position();
    auto const next  = response.Read<std::uint32_t>();
    response.Seek(start);
    Take(Entry(response));
    if (!next) break;
    if (next < response.Position() - start || next > limit - start) response.Invalid("invalid directory entry offset");
    response.Seek(start + next);
  }
  return length != 0;
}
auto Listing::Full() const noexcept -> bool {
  return _count == _out.size();
}
auto Listing::Count() const noexcept -> std::size_t {
  return _count;
}
auto Listing::Take(sdlrdp_dirent const& entry) -> void {
  if (!Listed(entry)) return;
  if (_skipped++ < _offset) return;
  Expects(!Full(), "a listed entry has room in the output");
  _out[_count++] = entry;
}
auto Entry(DrivePacket& packet) -> sdlrdp_dirent {
  constexpr std::size_t end_of_file_offset         = 40;
  constexpr std::size_t allocation_size_field_size = 8;
  packet.Skip(end_of_file_offset);
  sdlrdp_dirent entry{ };
  entry.size = packet.Read<std::uint64_t>();
  packet.Skip(allocation_size_field_size);
  entry.directory = (packet.Read<std::uint32_t>() & std::to_underlying(FileAttribute::Directory)) != 0;
  auto const length = packet.Read<std::uint32_t>();
  auto const name   = packet.Text(length);
  if (name.size() >= sizeof(entry.name)) throw EntryNameTooLong{ name.size(), sizeof(entry.name) - 1 };
  CopyTerminated(entry.name, name);
  return entry;
}
// MS-RDPEFS 2.2.3.3.10 DR_DRIVE_QUERY_DIRECTORY_REQ: the pattern only on the first query.
auto DirectoryQuery(bool first, std::span<std::byte const> pattern) -> DrivePacket {
  if (first) Expects(!pattern.empty(), "the first query carries its pattern");
  constexpr std::size_t padding_after_path_length = 23;
  DrivePacket           packet;
  packet.Write(std::to_underlying(InformationClass::Directory));
  packet.Write(std::uint8_t{ first });
  packet.Write(Narrowed<std::uint32_t>(first ? pattern.size() : 0));
  packet.Zero(padding_after_path_length);
  if (first) packet.Append(pattern);
  return packet;
}
}
