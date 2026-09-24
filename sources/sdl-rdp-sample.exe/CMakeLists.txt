Init_submodule()
Link_dependencies(SDL3 TEST freerdp-client freerdp winpr oxbox::platform)
target_link_libraries(sdl-rdp-sample PRIVATE OpenSSL::Crypto)
if(TARGET sdl-rdp-sample-tests)
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/bench-tests.cmake")
  target_sources(sdl-rdp-sample-tests PRIVATE ../sdl-rdp-backend.so/avc.cpp ../sdl-rdp-backend.so/avc-encoder.cpp
    ../sdl-rdp-backend.so/descriptor.cpp ../sdl-rdp-backend.so/drive-packet.cpp
    ../sdl-rdp-backend.so/malformed-response.cpp ../sdl-rdp-backend.so/test-logs.test.cpp
    ../sdl-rdp-backend.so/client.test.cpp ../sdl-rdp-backend.so/release-client.test.cpp
    ../sdl-rdp-backend.so/client-channels.test.cpp ../sdl-rdp-backend.so/frame-observer.test.cpp
    ../sdl-rdp-backend.so/display-client.test.cpp ../sdl-rdp-backend.so/sound-client.test.cpp
    ../sdl-rdp-backend.so/sound-protocol.test.cpp ../sdl-rdp-backend.so/tone-measurements.test.cpp
    ../sdl-rdp-backend.so/share-drive.test.cpp ../sdl-rdp-backend.so/drive-observer.test.cpp
    ../sdl-rdp-backend.so/clipboard-client.test.cpp
    ../sdl-rdp-backend.so/test-picture-update-hook.test.cpp)
  target_link_libraries(sdl-rdp-sample-tests PRIVATE OpenSSL::Crypto
    nv-codec-headers::nv-codec-headers ${CMAKE_DL_LIBS})
  find_path(FREERDP_HEADERS freerdp/freerdp.h PATH_SUFFIXES freerdp3 REQUIRED)
  target_include_directories(sdl-rdp-sample-tests PRIVATE ${FREERDP_HEADERS} ../sdl-rdp-backend.so)
  target_link_libraries(sdl-rdp-sample-tests PRIVATE ${SDLRDP_TEST_SSL})
endif()
