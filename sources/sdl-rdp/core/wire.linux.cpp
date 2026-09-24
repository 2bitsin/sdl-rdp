#include <sdl-rdp/core/refresh.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <linux/sockios.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

namespace Backend {
auto SampleWire(int descriptor) -> WireSample {
  utilities::Expects(descriptor >= 0, "peer socket is open");
  tcp_info  info   { };
  socklen_t length = sizeof(info);
  int       outq   = 0;
  if (getsockopt(descriptor, IPPROTO_TCP, TCP_INFO, &info, &length) != 0 || ioctl(descriptor, SIOCOUTQ, &outq) != 0)
    return { };
  if (length < offsetof(tcp_info, tcpi_delivery_rate) + sizeof(info.tcpi_delivery_rate)) return { };
  return { .available     = true,
           .outq          = unsigned(outq),
           .notsent       = info.tcpi_notsent_bytes,
           .unacked       = info.tcpi_unacked,
           .rtt           = info.tcpi_rtt,
           .mss           = info.tcpi_snd_mss,
           .delivery_rate = info.tcpi_delivery_rate };
}
}
