#include <sdl-rdp/link/wire.hpp>

#include <sdl-rdp/configuration/refresh.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <cstdint>
#include <linux/sockios.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

namespace sdl_rdp::link::detail::wire {
using sdl_rdp::utilities::DescriptorOf;
using sdl_rdp::utilities::Narrowed;

auto SampleWire(NativeSocket socket) -> WireSample {
  auto const descriptor = DescriptorOf(socket);
  tcp_info   info       { };
  socklen_t  length     = sizeof(info);
  int        outq       = 0;
  if (getsockopt(descriptor, IPPROTO_TCP, TCP_INFO, &info, &length) != 0 || ioctl(descriptor, SIOCOUTQ, &outq) != 0)
    return { };
  if (length < offsetof(tcp_info, tcpi_delivery_rate) + sizeof(info.tcpi_delivery_rate)) return { };
  return { .available     = true,
           .outq          = Narrowed<std::uint32_t>(outq),
           .notsent       = info.tcpi_notsent_bytes,
           .unacked       = info.tcpi_unacked,
           .rtt           = info.tcpi_rtt,
           .mss           = info.tcpi_snd_mss,
           .delivery_rate = info.tcpi_delivery_rate };
}
}
