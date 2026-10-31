cmake_minimum_required(VERSION 3.20)
project(deskstrate_host CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static runtime: one .exe, no Visual C++ redistributable needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(deskstrate-host WIN32 main.cpp agent.cpp displays.cpp log.cpp)
target_include_directories(deskstrate-host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/kvm_proto/include)
target_compile_definitions(deskstrate-host PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_link_libraries(deskstrate-host PRIVATE ws2_32 shell32 ole32 user32 advapi32 wtsapi32)
if(MINGW)
    target_link_options(deskstrate-host PRIVATE -static)
endif()
