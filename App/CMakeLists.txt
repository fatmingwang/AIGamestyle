cmake_minimum_required(VERSION 3.25)
project(GameStyleAI CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(GameStyleAI WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/AppConfig.cpp
    src/PipelineCommands.cpp
    src/PipelineEngine.cpp
    src/HeadlessRunner.cpp
    src/LogParser.cpp
    src/StepListWidget.cpp
    src/ResourceReplaceWidget.cpp
    src/AtlasWidget.cpp
    src/AtlasOps.cpp
    src/SymbolGroupWidget.cpp
    src/GroupBuilderWidget.cpp
    src/ImageWithPromptWidget.cpp
    src/Text2ImageWidget.cpp
    src/PoseGeneratorWidget.cpp
    src/LastDir.cpp
    src/LangManager.cpp
)

target_include_directories(GameStyleAI PRIVATE src)
target_link_libraries(GameStyleAI PRIVATE Qt6::Widgets Qt6::Network)

# Copy Qt runtime DLLs next to the exe on Windows
if(WIN32)
    add_custom_command(TARGET GameStyleAI POST_BUILD
        COMMAND "${Qt6_DIR}/../../../bin/windeployqt.exe"
                "$<TARGET_FILE:GameStyleAI>"
        COMMENT "Running windeployqt"
        VERBATIM
    )
endif()
