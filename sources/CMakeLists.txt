enable_language(C)
Require(FreeRDP-Server VERSION ">=3" SYSTEM)
Require(FreeRDP VERSION ">=3" SYSTEM)
Require(WinPR VERSION ">=3" SYSTEM)
Require(FreeRDP-Client VERSION ">=3" SYSTEM TEST)
Require(GTest VERSION ">=1.17.0" TEST CONAN gtest COMPONENTS gtest gtest_main)
Require(oxbox VERSION "0.30.0.344" CONAN oxbox)
Require(nv-codec-headers VERSION "13.0.19.0" CONAN nv-codec-headers)
Scan_subdirectories()
if(TARGET sdl-rdp-backend-tests)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME shape-lint COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/bin/lint-shape.py --allow ${CMAKE_SOURCE_DIR}/bin/lint-shape.allow
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME clone-lint COMMAND ${CMAKE_SOURCE_DIR}/bin/lint-clones.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(clone-lint PROPERTIES SKIP_RETURN_CODE 77)
endif()
