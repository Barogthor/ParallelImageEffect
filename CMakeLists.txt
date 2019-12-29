cmake_minimum_required(VERSION 3.12)
set(PROJECT_NAME ParallelImageEffect)
set(EXE_NAME "apply-effect")

project(${PROJECT_NAME})
set(CMAKE_C_STANDARD 11)

add_executable( ${EXE_NAME}
        src/bitmap.c
        src/bitmap.h
        src/info.c
        src/info.h
        src/apply-effect.c
)