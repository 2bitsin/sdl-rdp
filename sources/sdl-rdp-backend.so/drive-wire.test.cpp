#include "_detail/test-drive.hpp"
namespace DriveGate {
TEST_F(Drive, MalformedChannelKeepsVideoSession) {
  Write("file", Pattern(3 * 1024 * 1024));
  auto file = Open("file");
  ASSERT_NE(file, nullptr);
  pump.request_stop(); pump.join();
  Headless::DriveObserver observer(*client);
  observer.hold = true;
  auto read = std::async(std::launch::async, [&] {
    std::string bytes(3 * 1024 * 1024, '\0');
    return sdlrdp_drive_read(handle.get(), file, 0, bytes.data(), bytes.size());
  });
  ASSERT_TRUE(client->Until([&] { return observer.requests == 8; }));
  auto instance = client->instance.get();
  auto channel = freerdp_channels_get_id_by_name(instance, RDPDR_CHANNEL_NAME);
  BYTE malformed[]{0x72, 0x44, 0x41, 0x44};
  ASSERT_TRUE(instance->SendChannelData(instance, channel, malformed, sizeof(malformed)));
  ASSERT_TRUE(client->Until([&] {
    sdlrdp_drive value{};
    return sdlrdp_drive_list(handle.get(), &value, 1) == 0;
  }));
  ASSERT_EQ(read.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(read.get(), -1);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "Drive channel ended: Truncated drive response."), 1u);
  EXPECT_EQ(sdlrdp_drive_close(handle.get(), file), -1);
  sdlrdp_event events[32]{};
  auto count = sdlrdp_poll(handle.get(), events, 32);
  EXPECT_TRUE(std::ranges::any_of(std::span(events, count), [&](auto const& event) {
    return event.type == SDLRDP_DRIVE && !event.drive.added && event.drive.id == drive;
  }));
  std::vector<UINT32> pixels(320 * 200, 0x00446688);
  sdlrdp_rect damage{0, 0, 320, 200};
  ASSERT_EQ(sdlrdp_present(handle.get(), pixels.data(), 320 * 4, 320, 200, &damage, 1), 0);
  ASSERT_TRUE(client->Until([&] { return client->Matches(pixels); }));
}

TEST_F(Drive, MalformedInformationKeepsVideoSession) {
  Write("file", "data");
  auto file = Open("file");
  ASSERT_NE(file, nullptr);
  pump.request_stop(); pump.join();
  Headless::DriveObserver observer(*client);
  observer.hold = true;
  auto stat = std::async(std::launch::async, [&] {
    sdlrdp_stat info{};
    auto result = sdlrdp_drive_fstat(handle.get(), file, &info);
    return std::pair(result, std::string(sdlrdp_last_error()));
  });
  ASSERT_TRUE(client->Until([&] { return observer.requests == 1; }));
  auto request = observer.io.front();
  auto device = request.Get(4); request.Skip(4);
  auto id = request.Get(4);
  EXPECT_EQ(request.Get(4), IRP_MJ_QUERY_INFORMATION); request.Skip(4);
  EXPECT_EQ(request.Get(4), FileBasicInformation);
  Backend::DrivePacket response;
  response.Put(RDPDR_CTYP_CORE, 2); response.Put(PAKID_CORE_DEVICE_IOCOMPLETION, 2);
  response.Put(device); response.Put(id); response.Put(STATUS_SUCCESS);
  response.Put(0);
  auto warnings = Logged(SDLRDP_LOG_WARN, "");
  ASSERT_TRUE(observer.Send(response));
  ASSERT_TRUE(client->Until([&] { return stat.wait_for(0s) == std::future_status::ready; }));
  auto [result, error] = stat.get();
  EXPECT_EQ(result, -1); EXPECT_EQ(error, "Truncated drive response.");
  sdlrdp_drive value{};
  EXPECT_EQ(sdlrdp_drive_list(handle.get(), &value, 1), 0);
  EXPECT_EQ(sdlrdp_drive_close(handle.get(), file), -1);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "") - warnings, 1u);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "Drive channel ended: Truncated drive response."), 1u);
  std::vector<UINT32> pixels(320 * 200, 0x00446688);
  sdlrdp_rect damage{0, 0, 320, 200};
  ASSERT_EQ(sdlrdp_present(handle.get(), pixels.data(), 1280, 320, 200, &damage, 1), 0);
  ASSERT_TRUE(client->Until([&] { return client->Matches(pixels); }));
  RecordProperty("trace", "FileBasicInformation: Length=0; fstat=-1; Truncated drive response.; "
    "drives=0; WARN=1: Drive channel ended: Truncated drive response.; video matches");
}

TEST_F(Drive, SlidingWindowRefillsOnOutOfOrderCompletion) {
  Write("file", "data");
  auto file = Open("file");
  ASSERT_NE(file, nullptr);
  pump.request_stop(); pump.join();
  Headless::DriveObserver observer(*client);
  observer.hold = true;
  std::string bytes(10 * 65536, '\0');
  auto read = std::async(std::launch::async, [&] {
    return sdlrdp_drive_read(handle.get(), file, 0, bytes.data(), bytes.size());
  });
  ASSERT_TRUE(client->Until([&] { return observer.requests == 8; }));
  auto complete = [&](size_t index) {
    auto request = observer.io[index];
    auto device = request.Get(4);
    request.Skip(4);
    auto id = request.Get(4);
    Backend::DrivePacket response;
    response.Put(RDPDR_CTYP_CORE, 2); response.Put(PAKID_CORE_DEVICE_IOCOMPLETION, 2);
    response.Put(device); response.Put(id); response.Put(STATUS_SUCCESS);
    response.Put(65536); response.bytes.resize(response.bytes.size() + 65536, 'x');
    EXPECT_TRUE(observer.Send(response));
  };
  complete(7);
  ASSERT_TRUE(client->Until([&] { return observer.requests == 9; }));
  complete(0);
  ASSERT_TRUE(client->Until([&] { return observer.requests == 10; }));
  for (size_t index = 1; index < 10; ++index) if (index != 7) complete(index);
  ASSERT_TRUE(client->Until([&] { return read.wait_for(0s) == std::future_status::ready; }));
  EXPECT_EQ(read.get(), int(bytes.size()));
  EXPECT_EQ(bytes, std::string(bytes.size(), 'x'));
  observer.hold = false;
  auto close = std::async(std::launch::async, [&] { return sdlrdp_drive_close(handle.get(), file); });
  ASSERT_TRUE(client->Until([&] { return close.wait_for(0s) == std::future_status::ready; }));
  EXPECT_EQ(close.get(), 0);
}

TEST_F(Drive, UnicodeWireNameAndRecoverableAnnouncements) {
  pump.request_stop(); pump.join();
  Headless::DriveObserver observer(*client);
  observer.hold = true;
  std::string_view label = "żółw";
  auto wide = Backend::TranscodeRange<std::vector<uint8_t>>(std::as_bytes(std::span(label)), {},
    {oxbox::utilities::Encoding::UTF16, std::endian::little});
  wide.resize(wide.size() + 2);
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_FILESYSTEM, 100, wide)));
  std::vector<uint8_t> long_name(600, 'x'); long_name.push_back(0);
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_FILESYSTEM, 101, long_name)));
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_FILESYSTEM, 102, std::array<uint8_t, 2>{0xff, 0})));
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_PRINT, 103, {})));
  ASSERT_TRUE(client->Until([&] {
    return std::ranges::any_of(observer.replies, [](auto const& reply) { return reply.first == 103; });
  }));
  sdlrdp_drive drives[8]{};
  ASSERT_EQ(sdlrdp_drive_list(handle.get(), drives, 8), 4);
  EXPECT_STREQ(drives[1].name, "żółw");
  EXPECT_EQ(std::string(drives[2].name), std::string(511, 'x'));
  EXPECT_STREQ(drives[3].name, "dos");
  auto rejected = std::ranges::find(observer.replies, 103u, &std::pair<unsigned, unsigned>::first);
  ASSERT_NE(rejected, observer.replies.end());
  EXPECT_EQ(rejected->second, STATUS_NOT_SUPPORTED);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "truncating"), 1u);
  EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "Using DOS name"), 1u);
  EXPECT_EQ(Logged(SDLRDP_LOG_INFO, "extended PDU"), 1u);
}
TEST_F(Drive, UnknownCompletionIsIgnored) {
  pump.request_stop(); pump.join();
  Headless::DriveObserver observer(*client);
  Backend::DrivePacket packet;
  packet.Put(RDPDR_CTYP_CORE, 2); packet.Put(PAKID_CORE_DEVICE_IOCOMPLETION, 2);
  packet.Put(0); packet.Put(UINT32_MAX); packet.Put(STATUS_SUCCESS);
  ASSERT_TRUE(observer.Send(packet));
  ASSERT_TRUE(client->Until([&] { return Logged(SDLRDP_LOG_WARN, "Unknown drive completion id") == 1; }));
  sdlrdp_drive value{};
  EXPECT_EQ(sdlrdp_drive_list(handle.get(), &value, 1), 1);
  std::vector<UINT32> pixels(320 * 200, 0x00446688);
  sdlrdp_rect damage{0, 0, 320, 200};
  ASSERT_EQ(sdlrdp_present(handle.get(), pixels.data(), 1280, 320, 200, &damage, 1), 0);
  ASSERT_TRUE(client->Until([&] { return client->Matches(pixels); }));
}
}
