#include <sdl-rdp/sample-gate.test/frame/first-size.hpp>

#include <sdl-rdp/headless-client.test/client/handles.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>

namespace sdl_rdp::sample_gate_test::frame::detail::first_size {
using sdl_rdp::headless_client_test::client::ClientHandle;
using sdl_rdp::headless_client_test::utilities::ObserverSet;
using sdl_rdp::utilities::Expects;

FirstFrameSize::FirstFrameSize(Client& value) : client(value), original_connect(ClientHandle(value).PostConnect) {
  Expects(original_connect, "original connection callback is installed");
  ObserverSet::Of(*client.Instance()->context).Add(*this);
  // abi: pConnectCallback, BOOL is int
  client.Instance()->PostConnect = [](freerdp* instance) -> int {
    Expects(instance != nullptr, "FreeRDP instance exists");
    Expects(instance->context != nullptr, "the connecting client has its context");
    return ObserverSet::Of(*instance->context).Held<FirstFrameSize>()->Connect(*instance);
  };
}
FirstFrameSize::~FirstFrameSize() {
  client.Instance()->PostConnect = original_connect;
  if (paint_installed) client.Instance()->context->update->EndPaint = original_paint;
  ObserverSet::Of(*client.Instance()->context).Remove<FirstFrameSize>();
}
auto FirstFrameSize::Received() const -> bool {
  return received;
}
auto FirstFrameSize::Width() const -> int {
  return width;
}
auto FirstFrameSize::Height() const -> int {
  return height;
}
auto FirstFrameSize::Connect(freerdp& instance) -> bool {
  if (!original_connect(&instance)) return false;
  original_paint = instance.context->update->EndPaint;
  // abi: pEndPaint, BOOL is int
  instance.context->update->EndPaint = [](rdpContext* context) -> int {
    Expects(context != nullptr, "callback context exists");
    return ObserverSet::Of(*context).Held<FirstFrameSize>()->Paint(*context);
  };
  paint_installed                    = true;
  return true;
}
auto FirstFrameSize::Paint(rdpContext& context) -> bool {
  Expects(context.gdi, "decoded framebuffer exists");
  if (!received) {
    width    = context.gdi->width;
    height   = context.gdi->height;
    received = true;
  }
  return original_paint ? original_paint(&context) : true;
}
}
