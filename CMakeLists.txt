cmake_minimum_required(VERSION 2.8)

project(craft)

FILE(GLOB SOURCE_FILES src/*.c)

add_executable(
    craft
    ${SOURCE_FILES}
    deps/lodepng/lodepng.c
    deps/noise/noise.c
    deps/tinycthread/tinycthread.c)

add_definitions(-std=c99 -O3)
include_directories(deps/lodepng)
include_directories(deps/noise)
include_directories(deps/tinycthread)
find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIR})
target_link_libraries(craft dl m pthread GL GLEW glfw sqlite3 ${CURL_LIBRARIES})
