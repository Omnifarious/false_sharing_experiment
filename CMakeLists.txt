cmake_minimum_required (VERSION 3.22.1)

project (false_sharing_experiment VERSION 0.1.0.0 LANGUAGES CXX)

find_package(fmt REQUIRED)

add_executable(experiment experiment.cpp)
set_property(TARGET experiment PROPERTY CXX_EXTENSIONS OFF)
target_compile_features(experiment PUBLIC cxx_std_23)
target_link_libraries(experiment fmt::fmt)
