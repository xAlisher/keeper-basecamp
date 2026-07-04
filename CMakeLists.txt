cmake_minimum_required(VERSION 3.14)
project(KeeperModulePlugin LANGUAGES CXX)

# keeper builds via logos-module-builder (nix build .#lgx / .#lgx-portable), which
# runs the codegen for the universal impl_header (metadata.json). QtNetwork powers
# the archive.org state machine + the localhost QTcpServer bridge; QtCore is linked
# by logos_module(). Both are used by the pimpl in keeper_impl.cpp.
if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)

    find_package(Qt6 REQUIRED COMPONENTS Network)

    logos_module(
        NAME keeper
        LINK_LIBRARIES Qt6::Network
        SOURCES
            src/keeper_impl.h
            src/keeper_impl.cpp
            src/keeper_http_bridge.h
            src/keeper_http_bridge.cpp
        INCLUDE_DIRS
            src
    )
    return()
endif()

message(FATAL_ERROR "LOGOS_MODULE_BUILDER_ROOT not set")
