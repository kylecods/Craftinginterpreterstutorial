cmake_minimum_required(VERSION 3.17)
project(VM_PRAC C)

set(CMAKE_C_STANDARD 99)

add_executable(vmprac main.c vm.c chunk.c memory.c debug.c value.c scanner.c
        compiler.c object.c table.c)