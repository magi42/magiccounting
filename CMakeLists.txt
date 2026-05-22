cmake_minimum_required(VERSION 3.16)

project(MagicCounting VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_executable(magiccounting
    src/AccountListDialog.cpp
    src/AccountListDialog.h
    src/DataStore.cpp
    src/DataStore.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/SplitEditorDialog.cpp
    src/SplitEditorDialog.h
    src/main.cpp
)

target_link_libraries(magiccounting PRIVATE Qt5::Widgets)
