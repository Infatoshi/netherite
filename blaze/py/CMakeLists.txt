# pybind11 gym.Env smoke wrapper for blaze (Wave 14).

cmake_minimum_required(VERSION 3.15)
project(mcsim_gym LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(mcsim_gym mcsim_gym.cpp)

target_include_directories(mcsim_gym PRIVATE
    ${CMAKE_SOURCE_DIR}/../core
)

target_compile_options(mcsim_gym PRIVATE -O2 -ffp-contract=off)

target_link_libraries(mcsim_gym PRIVATE m)

# Compile core logic as C++ translation unit linked into the module.
target_sources(mcsim_gym PRIVATE ${CMAKE_SOURCE_DIR}/../core/py_gym_bridge.cpp)
