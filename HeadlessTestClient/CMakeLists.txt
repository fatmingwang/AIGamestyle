cmake_minimum_required(VERSION 3.25)
project(HeadlessTestClient CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(HeadlessTestClient WIN32
    src/main.cpp
    src/ClientWindow.cpp
)

target_include_directories(HeadlessTestClient PRIVATE src)
target_link_libraries(HeadlessTestClient PRIVATE Qt6::Widgets Qt6::Network)

# Copy Qt runtime DLLs next to the exe on Windows (same pattern as App/CMakeLists.txt)
if(WIN32)
    add_custom_command(TARGET HeadlessTestClient POST_BUILD
        COMMAND "${Qt6_DIR}/../../../bin/windeployqt.exe"
                "$<TARGET_FILE:HeadlessTestClient>"
        COMMENT "Running windeployqt"
        VERBATIM
    )
endif()
