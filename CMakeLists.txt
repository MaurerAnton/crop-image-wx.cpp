cmake_minimum_required(VERSION 3.20)
project(crop_tool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

add_executable(crop_tool WIN32 MACOSX_BUNDLE crop_tool.cpp)
target_link_libraries(crop_tool ${wxWidgets_LIBRARIES})
