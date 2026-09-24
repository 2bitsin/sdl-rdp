set(CMAKE_CXX_STANDARD 23)
find_path(FREERDP_HEADERS freerdp/freerdp.h PATH_SUFFIXES freerdp3 REQUIRED)
Require(OpenSSL VERSION ">=3" SYSTEM)
# FreeRDP uses host OpenSSL; oxbox's imported OpenSSL target belongs to Conan.
find_library(SDLRDP_TEST_SSL NAMES ssl PATHS ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES} NO_DEFAULT_PATH REQUIRED)
find_library(SDLRDP_HOST_CRYPTO NAMES crypto PATHS ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES} NO_DEFAULT_PATH REQUIRED)
# Certificate generation must share FreeRDP's host OpenSSL, without static interposition.
set_property(TARGET OpenSSL::SSL PROPERTY INTERFACE_LINK_LIBRARIES "${SDLRDP_TEST_SSL}")
set_property(TARGET OpenSSL::Crypto PROPERTY INTERFACE_LINK_LIBRARIES "${SDLRDP_HOST_CRYPTO}")
Init_submodule(PUBLISH_SYMBOLS)
Link_dependencies(freerdp-server freerdp winpr oxbox::platform
  nv-codec-headers::nv-codec-headers ${CMAKE_DL_LIBS}
  TEST freerdp-client ${SDLRDP_TEST_SSL} ${SDLRDP_HOST_CRYPTO})
target_link_libraries(sdl-rdp-backend PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(sdl-rdp-backend PUBLIC ${FREERDP_HEADERS})
target_link_options(sdl-rdp-backend PRIVATE "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
set_property(TARGET sdl-rdp-backend APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/exports.map)
get_target_property(SDLRDP_SOURCES sdl-rdp-backend SOURCES)
set(SDLRDP_INTERNAL_SOURCES ${SDLRDP_SOURCES})
list(FILTER SDLRDP_INTERNAL_SOURCES INCLUDE REGEX "\\.cpp$")
# The ABI translation units stay in the library so the tests exercise its exports.
list(FILTER SDLRDP_INTERNAL_SOURCES EXCLUDE REGEX "(^|/)(backend|drive-files|auth-pair)\\.cpp$")
list(REMOVE_ITEM SDLRDP_SOURCES ${SDLRDP_INTERNAL_SOURCES})
set_property(TARGET sdl-rdp-backend PROPERTY SOURCES ${SDLRDP_SOURCES})
add_library(sdl-rdp-backend-internal OBJECT ${SDLRDP_INTERNAL_SOURCES})
set_property(TARGET sdl-rdp-backend-internal PROPERTY POSITION_INDEPENDENT_CODE ON)
foreach(SDLRDP_PROPERTY INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS)
  set_property(TARGET sdl-rdp-backend-internal PROPERTY ${SDLRDP_PROPERTY}
               "$<TARGET_PROPERTY:sdl-rdp-backend,${SDLRDP_PROPERTY}>")
endforeach()
target_link_libraries(sdl-rdp-backend-internal PUBLIC freerdp-server freerdp winpr oxbox::platform
  nv-codec-headers::nv-codec-headers OpenSSL::SSL OpenSSL::Crypto ${CMAKE_DL_LIBS})
target_link_libraries(sdl-rdp-backend PRIVATE sdl-rdp-backend-internal)
if(TARGET sdl-rdp-backend-tests)
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/bench-tests.cmake")
  target_sources(sdl-rdp-backend-tests PRIVATE ../SDL3.so/rdp/SDL_rdpini.cpp ../SDL3.so/rdp/SDL_rdprefresh.cpp)
  target_link_libraries(sdl-rdp-backend-tests PRIVATE sdl-rdp-backend-internal SDL3)
  # tls-race.test/ interposes libcrypto and libc symbols, so it must not share a process with the other tests.
  file(GLOB TLS_RACE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tls-race.test/*.cpp)
  get_target_property(BACKEND_TEST_SOURCES sdl-rdp-backend-tests SOURCES)
  list(REMOVE_ITEM BACKEND_TEST_SOURCES ${TLS_RACE_SOURCES})
  set_property(TARGET sdl-rdp-backend-tests PROPERTY SOURCES ${BACKEND_TEST_SOURCES})
  add_executable(sdl-rdp-backend-tls-race-tests ${TLS_RACE_SOURCES} support.test/child-process.cpp
    client.test.cpp release-client.test.cpp client-channels.test.cpp)
  target_include_directories(sdl-rdp-backend-tls-race-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(sdl-rdp-backend-tls-race-tests PRIVATE sdl-rdp-backend sdl-rdp-backend-internal
    GTest::gtest_main freerdp-client ${SDLRDP_TEST_SSL} ${SDLRDP_HOST_CRYPTO} ${CMAKE_DL_LIBS})
  target_link_options(sdl-rdp-backend-tls-race-tests PRIVATE
    "LINKER:--export-dynamic-symbol=BIO_meth_new,--export-dynamic-symbol=BIO_meth_set_write"
    "LINKER:--export-dynamic-symbol=BIO_meth_set_create,--export-dynamic-symbol=SSL_use_PrivateKey"
    "LINKER:--export-dynamic-symbol=SSL_connect")
  gtest_discover_tests(sdl-rdp-backend-tls-race-tests
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS sdl-rdp-backend FIXTURES_REQUIRED buildutil-driver)
endif()
