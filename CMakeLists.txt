cmake_minimum_required(VERSION 3.2)
project(libnd4j)
set( CMAKE_VERBOSE_MAKEFILE on )
option(TESTS "Compile and make tests for the code?" OFF)
option(PTX "Compile ptx" OFF)
option(CUBIN "Compile cubin" OFF)
option(ECLIPSE "Generate eclipse files" OFF)
option(BLAS "Compile blas shared library for either cuda or cpu" ON)
option(DEV ON)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
#ensure we create lib files
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)
find_path(STDIO_INCLUDE_PATH stdio.h)

set(CMAKE_CXX_FLAGS_RELEASE  "-Wall -fopenmp -std=c++11 -fassociative-math -funsafe-math-optimizations")
set(CMAKE_CXX_FLAGS_DEBUG "-Wall -g -O0 -fopenmp -std=c++11")



IF(${CMAKE_SYSTEM_PROCESSOR} MATCHES "ppc64*")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=native")
ELSE()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
ENDIF()



include_directories(include)
add_subdirectory(include)
add_subdirectory(src)

find_package(CUDA)

if(DEV)
if (CUDA_FOUND)
    add_definitions(-D__CUDACC__)
    message("CUDA found!")
    set( CUDA_ARCHITECTURE_MINIMUM "3.5" CACHE STRING "Minimum required CUDA compute capability" )
    include_directories(/usr/local/cuda/include)
    SET(CUDA_VERBOSE_BUILD ON)
    SET(CUDA_SEPARABLE_COMPILATION OFF)
    set(CUDA_COMPUTE_CAPABILITY "35")
    if (FATBIN)
        list(APPEND CUDA_NVCC_FLAGS "-DVERBOSE;-G;-g; -gencode arch=compute_20,code=sm_20 -gencode arch=compute_30,code=sm_30 -gencode arch=compute_35,code=sm_35 -gencode arch=compute_37,code=sm_37 -gencode arch=compute_50,code=sm_50 -gencode arch=compute_52,code=sm_52")
    else()
        list(APPEND CUDA_NVCC_FLAGS "-DVERBOSE;-G;-g; -gencode arch=compute_35,code=sm_35")
    endif()
endif(CUDA_FOUND)

set(CUDA_BLAS TRUE)
add_executable(main main.cpp)
endif(DEV)

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    include_directories("/usr/include")
    include_directories("/usr/local/include")
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")


if(ECLIPSE)
    message("ADDING ECLIPSE")
    add_subdirectory(eclipse)
endif()

if(PTX)
    message("ADDING PTX")
    add_subdirectory(ptx)
endif()

if(CUBIN)
    message("ADDING CUBIN")
    add_subdirectory(cubin)
endif()


if(BLAS)
    message("ADDING BLAS")
    add_subdirectory(blas)
endif()

if(FATBIN)
    message("ADDING FATBIN")
    add_subdirectory(fatbin)
endif()

if(TESTS)
    # packages
    message("ADDING TEST")
    add_subdirectory(test)
endif()
