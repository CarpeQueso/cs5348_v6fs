cmake_minimum_required(VERSION 3.6)
project(cs5348_v6fs)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES fsaccess.c
        v6fs.h
        v6fs.c)
add_executable(cs5348_v6fs ${SOURCE_FILES})