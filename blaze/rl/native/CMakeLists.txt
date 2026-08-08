cmake_minimum_required(VERSION 3.26)
project(netherite_ppo_native LANGUAGES CXX)

find_package(Torch REQUIRED)
find_package(CUDAToolkit REQUIRED)

add_executable(ppo_native ppo_native.cpp)
target_compile_features(ppo_native PRIVATE cxx_std_20)
target_compile_options(ppo_native PRIVATE -O3 -Wall -Wextra -Werror=return-type)
target_link_libraries(ppo_native PRIVATE ${TORCH_LIBRARIES} CUDA::cudart CUDA::nvtx3 dl)
set_property(TARGET ppo_native PROPERTY BUILD_RPATH_USE_ORIGIN FALSE)
