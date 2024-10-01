
cmake_minimum_required(VERSION 3.20)
project(mutate LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 23)

add_library(mutate INTERFACE)
target_include_directories(mutate INTERFACE include/)

add_executable(lifo)
target_sources(lifo PRIVATE src/lifo.cc)
target_link_libraries(lifo PUBLIC mutate)

add_executable(lifo_cas)
target_sources(lifo_cas PRIVATE src/lifo.cc)
target_link_libraries(lifo_cas PUBLIC mutate)
target_compile_definitions(lifo_cas PRIVATE ATOMIC_MUTATE_FORCE_CAS=1)


add_executable(arithmetic)
target_sources(arithmetic PRIVATE src/lifo.cc)
target_link_libraries(arithmetic PUBLIC mutate)

add_executable(arithmetic_cas)
target_sources(arithmetic_cas PRIVATE src/lifo.cc)
target_link_libraries(arithmetic_cas PUBLIC mutate)
target_compile_definitions(arithmetic_cas PRIVATE ATOMIC_MUTATE_FORCE_CAS=1)