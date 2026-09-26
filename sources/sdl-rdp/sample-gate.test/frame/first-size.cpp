#include <sdl-rdp/sample-gate.test/frame/first-size.hpp>

#include <sdl-rdp/headless-client.test/client/handles.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

namespace sdl_rdp::sample_gate_test::frame::detail::first_size {
using sdl_rdp::headless_client_test::client::ClientContext;
using sdl_rdp::headless_client_test::client::ClientHandle;
using sdl_rdp::headless_client_test::utilities::Delegated;
using sdl_rdp::headless_client_test::utilities::ObserverSet;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;

FirstFrameSize::FirstFrameSize(Client& value)
    : client(value), original_connect(ClientHandle(value).PostConnect), membership(ClientContext(value), *this) {
  Expects(original_connect, "original connection callback is installed");
  // abi: pConnectCallback, BOOL is int
  client.Instance()->PostConnect = [](freerdp* instance) -> int {
    Expects(instance != nullptr, "FreeRDP instance exists");
    Expects(instance->context != nullptr, "the connecting client has its context");
    return ObserverSet::Of(*instance->context).Held<FirstFrameSize>()->Connect(*instance);
  };
}
FirstFrameSize::~FirstFrameSize() {
  ClientHandle(client).PostConnect = original_connect;
  if (paint_installed) ClientContext(client).update->EndPaint = original_paint;
}
auto FirstFrameSize::Received() const -> bool {
  return received;
}
auto FirstFrameSize::Size() const -> Extent {
  return size;
}
auto FirstFrameSize::Connect(freerdp& instance) -> bool {
  if (!original_connect(&instance)) return false;
  original_paint                     = instance.context->update->EndPaint;
  instance.context->update->EndPaint = Delegated<&FirstFrameSize::Paint>;
  paint_installed                    = true;
  return true;
}
auto FirstFrameSize::Paint(rdpContext& context) -> bool {
  Expects(context.gdi, "decoded framebuffer exists");
  if (!received) {
    size = { .width  = Narrowed<std::uint32_t>(context.gdi->width),
             .height = Narrowed<std::uint32_t>(context.gdi->height) };
    received = true;
  }
  return original_paint ? original_paint(&context) : true;
}
}
