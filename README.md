# vk-video-decode


```bash
# Configure the build with CMake
cmake -S . -B build

# Build the project in Release mode
cmake --build build --config Release

# Run the executable
build\Release\vkvideo_min.exe
```

main()
├── create_vulkan_instance()
├── select_video_capable_device()
├── create_logical_device()
├── query_video_capabilities()
└── cleanup_vulkan()