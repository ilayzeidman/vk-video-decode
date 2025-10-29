cmake_minimum_required(VERSION 3.24)
project(vk-video-decode C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

add_executable(vk_video_decode
    src/main.c
)

target_link_libraries(vk_video_decode
    PRIVATE Vulkan::Vulkan
)
