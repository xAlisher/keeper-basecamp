cmake_minimum_required(VERSION 3.14)
project(KeeperModulePlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)

    find_package(Qt6 REQUIRED COMPONENTS Network)

    logos_module(
        NAME keeper
        LINK_LIBRARIES Qt6::Network
        SOURCES
            src/keeper_interface.h
            src/keeper_plugin.h
            src/keeper_plugin.cpp
            src/keeper_http_bridge.h
            src/keeper_http_bridge.cpp
        INCLUDE_DIRS
            src
    )
    return()
endif()

message(FATAL_ERROR "LOGOS_MODULE_BUILDER_ROOT not set")
