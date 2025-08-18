cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_COMPILER  CACHE FILEPATH "C++ compiler" FORCE)
set(CMAKE_C_COMPILER  CACHE FILEPATH "C compiler" FORCE)

project(main VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED COMPONENTS system thread json)

find_package(OpenSSL REQUIRED)

# Add the executable target.
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/BinanceExchange.cpp
    src/CoinbaseExchange.cpp
)

# Link the executable with its dependencies.
target_include_directories(${PROJECT_NAME} PUBLIC include)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Boost::system
    Boost::thread
    Boost::json
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Set common compiler options for all platforms.
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
)

# Define preprocessor macros for Windows.
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 crypt32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )
endif()