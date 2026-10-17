cmake_minimum_required(VERSION 3.20)
project(SimCore CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Pure C++ simulation core. No Unreal types; Unreal wraps it in a thin SimBridge module.
add_library(simcore
  src/json.cpp
  src/defs.cpp
  src/world.cpp
  src/setup.cpp
  src/life.cpp
  src/paths.cpp
  src/focus.cpp
  src/economy.cpp
  src/social.cpp
  src/events.cpp
  src/ambitions.cpp
  src/plans.cpp
  src/memory.cpp src/manor.cpp src/knowledge.cpp src/signals.cpp src/stress.cpp src/arcs.cpp src/hooks.cpp src/report.cpp src/minds.cpp src/concerns.cpp src/news.cpp src/land.cpp src/contest.cpp src/ask.cpp src/prerun.cpp
)
target_include_directories(simcore PUBLIC include)
target_compile_options(simcore PRIVATE -Wall -Wextra)

add_executable(chronicle tools/chronicle.cpp)
target_link_libraries(chronicle simcore)
add_executable(play tools/play.cpp)
target_link_libraries(play simcore)
add_executable(webplay tools/webplay.cpp)
target_link_libraries(webplay simcore)

find_package(Threads REQUIRED)
add_executable(stats tools/stats.cpp)
target_link_libraries(stats simcore Threads::Threads)

enable_testing()
add_executable(simcore_tests tests/tests.cpp)
target_link_libraries(simcore_tests simcore)
target_compile_definitions(simcore_tests PRIVATE SIMCORE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME simcore_tests COMMAND simcore_tests)
