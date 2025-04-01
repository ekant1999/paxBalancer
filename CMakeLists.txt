cmake_minimum_required(VERSION 3.16)
project(paxBalancer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(pax_balancer src/load_balancer.cpp)
target_include_directories(pax_balancer PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_options(pax_balancer PRIVATE -Wall -Wextra -Wpedantic)

add_executable(backend_server src/backend_server.cpp)
target_include_directories(backend_server PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_options(backend_server PRIVATE -Wall -Wextra -Wpedantic)

add_executable(pax_client src/client.cpp)
target_include_directories(pax_client PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_options(pax_client PRIVATE -Wall -Wextra -Wpedantic)
