cmake_minimum_required(VERSION 3.14)
project(KeeperUi LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found")
endif()

# Universal ui_qml module: REP_FILE runs repc on keeper_ui.rep; the generated
# *Plugin glue + logos_sdk.h (from metadata.json#dependencies) compile in.
logos_module(
    NAME keeper_ui
    REP_FILE src/keeper_ui.rep
    SOURCES
        src/keeper_ui_backend.h
        src/keeper_ui_backend.cpp
    INCLUDE_DIRS
        src
)
