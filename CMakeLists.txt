cmake_minimum_required(VERSION 3.16)
project(SnippetManager)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql)

set(CMAKE_AUTOMOC ON)

set(snippetmanager_SRCS
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/snippetmodel.cpp
    src/snippetmodel.h
    src/database.cpp
    src/database.h
)

add_executable(snippetmanager ${snippetmanager_SRCS})

target_link_libraries(snippetmanager
    Qt6::Core
    Qt6::Widgets
    Qt6::Sql
)