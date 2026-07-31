cmake_minimum_required(VERSION 3.27)
project(1brc CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-pedantic -Wall -Wextra -O1 -march=native -fno-omit-frame-pointer)
endif()
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-pedantic -Wall -Wextra -O3 -march=native -g0 -DNDEBUG -fomit-frame-pointer)
endif()
if (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_options(-pedantic -Wall -Wextra -O3 -march=native -fno-omit-frame-pointer -ggdb3)
endif()

add_executable(01_baseline 01_baseline.cpp)

find_package(Threads REQUIRED)
add_executable(solution solution.cpp)
target_link_libraries(solution PRIVATE Threads::Threads)

add_executable(solution2 solution2.cpp)
target_link_libraries(solution2 PRIVATE Threads::Threads)

add_executable(solution3 solution3.cpp)
target_link_libraries(solution3 PRIVATE Threads::Threads)

add_executable(solution4 solution4.cpp)
target_link_libraries(solution4 PRIVATE Threads::Threads)