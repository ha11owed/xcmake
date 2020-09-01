cmake_minimum_required(VERSION 3.1)

project(XCMake)

set(XCMAKE_SOURCES
    "Config.h" "Config.cpp"
    "CMaker.h" "CMaker.cpp"
    "CbpPatcher.h" "CbpPatcher.cpp"
    # Lib dependencies
    "file_system.h" "file_system.cpp"
    "tinyxml2.h" "tinyxml2.cpp")

set(XCMAKELIB ${PROJECT_NAME}_Lib)

add_library(${XCMAKELIB} STATIC ${XCMAKE_SOURCES})

add_executable(${PROJECT_NAME}
    ${XCMAKE_SOURCES}
    "main.cpp")

add_executable(${PROJECT_NAME}_Tests
    ${XCMAKE_SOURCES}
    "tests/runtests.cpp"
    # Tests
    "tests/CbpPatcherTests.cpp"
    "tests/CMakerTests.cpp"
    "tests/ConfigTests.cpp"
    # GTest
    "tests/gtest/gtest.h"
    "tests/gtest/gtest-all.cc")

target_link_libraries(${PROJECT_NAME} PRIVATE ${XCMAKELIB})
target_link_libraries(${PROJECT_NAME}_Tests PRIVATE ${XCMAKELIB})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${XCMAKELIB} Threads::Threads)
target_include_directories(${XCMAKELIB} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tests>)

if(CMAKE_DL_LIBS)
    target_link_libraries(${XCMAKELIB} ${CMAKE_DL_LIBS})
endif()

file(COPY "tests/data/testproject_input.cbp" "tests/data/testproject_output.cbp.xml" "tests/data/xcmake.json" DESTINATION ${CMAKE_BINARY_DIR})
