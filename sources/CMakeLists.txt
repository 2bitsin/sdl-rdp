enable_language(C)
Require(FreeRDP-Server   VERSION ">=3"          SYSTEM)
Require(FreeRDP          VERSION ">=3"          SYSTEM)
Require(WinPR            VERSION ">=3"          SYSTEM)
Require(FreeRDP-Client   VERSION ">=3"          SYSTEM TEST)
Require(GTest            VERSION ">=1.17.0"     TEST CONAN gtest COMPONENTS gtest gtest_main)
Require(oxbox            VERSION "0.32.3.357"   CONAN oxbox)
Require(nv-codec-headers VERSION "13.0.19.0"    CONAN nv-codec-headers)
Scan_subdirectories()
if(TARGET sdl-rdp-backend-tests)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME              shape-lint
           COMMAND           ${Python3_EXECUTABLE}
                             ${CMAKE_SOURCE_DIR}/bin/lint-shape.py --allow ${CMAKE_SOURCE_DIR}/bin/lint-shape.allow
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME              shape-lint-tests
           COMMAND           ${CMAKE_SOURCE_DIR}/bin/lint-shape-tests.sh ${Python3_EXECUTABLE}
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME              align-check
           COMMAND           ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/bin/align-columns.py --check sources
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME              align-columns-test
           COMMAND           ${CMAKE_SOURCE_DIR}/bin/test-align-columns.sh ${Python3_EXECUTABLE}
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(align-columns-test PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME              clone-lint
           COMMAND           ${CMAKE_SOURCE_DIR}/bin/lint-clones.sh
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  file(GLOB DRIVER_UNITS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/sources/SDL3.so/rdp/*.cpp)
  foreach(unit IN LISTS DRIVER_UNITS)
    get_filename_component(unit_name ${unit} NAME_WE)
    add_test(NAME    tidy-driver/${unit_name}
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/bin/tidy-driver.py
                     ${CMAKE_BINARY_DIR}/compile_commands.json --unit ${unit})
    set_tests_properties(tidy-driver/${unit_name} PROPERTIES TIMEOUT 300)
  endforeach()
  set_tests_properties(clone-lint shape-lint-tests PROPERTIES SKIP_RETURN_CODE 77)
endif()
