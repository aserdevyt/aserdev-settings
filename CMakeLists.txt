cmake_minimum_required(VERSION 3.16)
project(aser-settings C)

set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)

include_directories(${GTK4_INCLUDE_DIRS})
link_directories(${GTK4_LIBRARY_DIRS})
add_definitions(${GTK4_CFLAGS_OTHER})

add_executable(aser-settings main.c hypr.c)

target_compile_options(aser-settings PRIVATE ${GTK4_CFLAGS_OTHER})
target_include_directories(aser-settings PRIVATE ${GTK4_INCLUDE_DIRS} .)
target_link_libraries(aser-settings ${GTK4_LIBRARIES})
