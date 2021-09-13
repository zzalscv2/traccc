add_subdirectory(cpu)

if (TRACCC_BUILD_CUDA)
  add_subdirectory(cuda)
endif()

find_package(OpenMP COMPONENTS CXX)
if (OpenMP_CXX_FOUND)
    add_subdirectory(openmp)
    message(STATUS "OpenMP found! Include /examples/openmp subdirectory")
endif()