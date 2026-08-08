cmake_minimum_required(VERSION 3.26)
project(netherite_ppo_cgraph LANGUAGES CXX CUDA)

find_package(Torch REQUIRED)
find_package(CUDAToolkit REQUIRED)

add_executable(cgraph_train cgraph_train.cpp cgraph_kernels.cu)
target_compile_features(cgraph_train PRIVATE cxx_std_20)
target_compile_options(cgraph_train PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-O3;-Wall;-Wextra;-Werror=return-type>
  $<$<COMPILE_LANGUAGE:CUDA>:-O3>)
target_link_libraries(cgraph_train PRIVATE
  ${TORCH_LIBRARIES} CUDA::cudart CUDA::nvtx3 dl)
set_property(TARGET cgraph_train PROPERTY BUILD_RPATH_USE_ORIGIN FALSE)
set_property(TARGET cgraph_train PROPERTY CUDA_ARCHITECTURES "86;120")
