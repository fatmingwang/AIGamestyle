# ComfyLive -- ComfyUI's parameters as a form, the picture as it forms (README.md). Qt 6 Widgets + Network, like the
# GameStyleAI app next to it (../App); no other library.
cmake_minimum_required(VERSION 3.25)
project(ComfyLive CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(ComfyLive WIN32
    src/main.cpp
    src/MainWindow.h        src/MainWindow.cpp
    src/ComfyClient.h       src/ComfyClient.cpp
    src/ComfyProcess.h      src/ComfyProcess.cpp
    src/HeadlessServer.h    src/HeadlessServer.cpp
    src/MiniWebSocket.h     src/MiniWebSocket.cpp
    src/Workflow.h          src/Workflow.cpp
    src/ImageView.h         src/ImageView.cpp
    src/AudioPlayer.h       src/AudioPlayer.cpp
    src/Translator.h        src/Translator.cpp
)
target_include_directories(ComfyLive PRIVATE src)
if(MSVC)
    target_compile_options(ComfyLive PRIVATE /utf-8)   # the Traditional Chinese text in Translator.cpp
endif()
target_link_libraries(ComfyLive PRIVATE Qt6::Widgets Qt6::Network $<$<PLATFORM_ID:Windows>:winmm>)   # winmm: AudioPlayer (MCI)
# The local ComfyUI that "Start local ComfyUI" runs (main.py and its .venv); changeable in the window.
target_compile_definitions(ComfyLive PRIVATE COMFYLIVE_DEFAULT_COMFY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../_comfyui/ComfyUI")

# Visual Studio (make_vs_solution.bat): F5 starts ComfyLive; Qt's bin on PATH so it runs even before windeployqt.
# Command-line arguments (--start, --headless ...): Project > Properties > Debugging > Command Arguments.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ComfyLive)
set_target_properties(ComfyLive PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:ComfyLive>"
    VS_DEBUGGER_ENVIRONMENT "PATH=${Qt6_DIR}/../../../bin;%PATH%")

# The Qt DLLs next to the exe, so it runs from Explorer
if(WIN32)
    add_custom_command(TARGET ComfyLive POST_BUILD
        COMMAND "${Qt6_DIR}/../../../bin/windeployqt.exe" --no-translations "$<TARGET_FILE:ComfyLive>"
        COMMENT "Running windeployqt"
        VERBATIM)
    # --selftest / --screenshot / --headless draw off screen: Qt's "offscreen" platform, which windeployqt leaves out
    add_custom_command(TARGET ComfyLive POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${Qt6_DIR}/../../../plugins/platforms/qoffscreen$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:ComfyLive>/platforms/"
        VERBATIM)
endif()
