cmake_minimum_required(VERSION 3.25)

# Set vcpkg toolchain and triplet before project()
set(CMAKE_TOOLCHAIN_FILE "C:/msys64/home/user/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
set(VCPKG_TARGET_TRIPLET x64-mingw-dynamic CACHE STRING "Vcpkg target triplet")

# Set compilers for MSYS2 ucrt64
set(CMAKE_CXX_COMPILER "C:/msys64/ucrt64/bin/g++.exe" CACHE FILEPATH "C++ compiler" FORCE)
set(CMAKE_C_COMPILER "C:/msys64/ucrt64/bin/gcc.exe" CACHE FILEPATH "C compiler" FORCE)

project(main VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find dependencies
find_package(Boost REQUIRED COMPONENTS system thread json)
find_package(OpenSSL REQUIRED)
find_package(simdjson CONFIG REQUIRED)
find_package(quill CONFIG REQUIRED)

# Add the executable target
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/binance_pipeline.cpp
    src/binance_exchange.cpp
    src/binance_data_processor.cpp
    src/coinbase_pipeline.cpp
    src/coinbase_exchange.cpp
    src/coinbase_data_processor.cpp
    src/kraken_pipeline.cpp
    src/kraken_exchange.cpp
    src/kraken_data_processor.cpp
)

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC include)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Boost::system
    Boost::thread
    Boost::json
    OpenSSL::SSL
    OpenSSL::Crypto
    simdjson::simdjson
    quill::quill
)

# Compiler options
target_compile_options(${PROJECT_NAME} PRIVATE
    -O3 -march=native -mtune=native
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
)

# Windows-specific settings
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 crypt32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )
endif()