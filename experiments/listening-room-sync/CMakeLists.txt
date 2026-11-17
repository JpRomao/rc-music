# Spike descartável S1.1 da sala de escuta. Fora do build do produto e do CTest.
cmake_minimum_required(VERSION 3.25)
project(listening_room_sync LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Multimedia)

add_executable(listening_room_sync main.cpp LoopbackRecorder.cpp LoopbackRecorder.h)
target_link_libraries(listening_room_sync PRIVATE Qt6::Core Qt6::Multimedia ole32)
target_compile_definitions(listening_room_sync PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(listening_room_sync PRIVATE /W4 /utf-8 /permissive-)
endif()
