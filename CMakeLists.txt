cmake_minimum_required(VERSION 3.22)
project(rmtpserver VERSION 0.3.0 LANGUAGES CXX)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build with Visual Studio for Windows x64.")
endif()

set(OBS_SOURCE_DIR "" CACHE PATH "OBS Studio source checkout matching your installation")
set(OBS_INSTALL_DIR "C:/Program Files/obs-studio" CACHE PATH "OBS Studio installation")
set(obs_headers "${OBS_SOURCE_DIR}/libobs")
set(frontend_headers "${OBS_SOURCE_DIR}/frontend/api")
if(NOT EXISTS "${obs_headers}/obs-module.h" OR NOT EXISTS "${frontend_headers}/obs-frontend-api.h")
    message(FATAL_ERROR "Set OBS_SOURCE_DIR to an OBS Studio source checkout.")
endif()

# Windows OBS releases include DLLs. Generate import libraries from their exports.
function(import_obs_library name)
    set(dll "${OBS_INSTALL_DIR}/bin/64bit/${name}.dll")
    if(NOT EXISTS "${dll}")
        message(FATAL_ERROR "Cannot find ${dll}. Set OBS_INSTALL_DIR to an OBS Studio installation.")
    endif()
    set(import_lib "${CMAKE_CURRENT_BINARY_DIR}/${name}.lib")
    add_custom_command(OUTPUT "${import_lib}"
        COMMAND "${CMAKE_COMMAND}" "-DOBS_DLL=${dll}" "-DIMPORT_LIB=${import_lib}"
            "-DLINK_TOOL=${CMAKE_LINKER}" "-DLIB_TOOL=${CMAKE_AR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ImportObs.cmake"
        DEPENDS "${dll}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ImportObs.cmake"
        VERBATIM)
    add_custom_target("${name}-import" DEPENDS "${import_lib}")
    add_library("${name}" SHARED IMPORTED)
    set_target_properties("${name}" PROPERTIES IMPORTED_IMPLIB "${import_lib}" IMPORTED_LOCATION "${dll}")
    add_dependencies("${name}" "${name}-import")
endfunction()

import_obs_library(obs)
import_obs_library(obs-frontend-api)
find_package(Qt6 6.11.1 REQUIRED COMPONENTS Widgets Network)

set(MEDIAMTX_DIR "" CACHE PATH "Extracted MediaMTX v1.21.4 Windows x64 release")
if(NOT MEDIAMTX_DIR)
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    FetchContent_Declare(mediamtx
        URL https://github.com/bluenviron/mediamtx/releases/download/v1.21.4/mediamtx_v1.21.4_windows_amd64.zip
        URL_HASH SHA256=835645b2700ffce4d2b6cb04bfa0aadce95335da6dfc0d7c2114e6da585734f8)
    FetchContent_MakeAvailable(mediamtx)
    set(MEDIAMTX_DIR "${mediamtx_SOURCE_DIR}")
endif()
if(NOT EXISTS "${MEDIAMTX_DIR}/mediamtx.exe" OR NOT EXISTS "${MEDIAMTX_DIR}/LICENSE")
    message(FATAL_ERROR "MEDIAMTX_DIR must contain the MediaMTX executable and LICENSE.")
endif()

set(OBS_RELEASE_CANDIDATE 0)
set(OBS_BETA 0)
configure_file("${obs_headers}/obsconfig.h.in" generated/obsconfig.h)

add_library(rmtpserver MODULE src/relay.cpp src/relay_session.cpp src/dialog.cpp)
target_compile_features(rmtpserver PRIVATE cxx_std_17)
target_compile_options(rmtpserver PRIVATE /W4 /utf-8)
target_include_directories(rmtpserver SYSTEM PRIVATE "${obs_headers}" "${frontend_headers}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(rmtpserver PRIVATE obs obs-frontend-api Qt6::Widgets Qt6::Network)

install(TARGETS rmtpserver LIBRARY DESTINATION rmtpserver
    RUNTIME DESTINATION rmtpserver)
install(DIRECTORY data/ DESTINATION rmtpserver/data)
install(FILES "${MEDIAMTX_DIR}/mediamtx.exe" "${MEDIAMTX_DIR}/LICENSE"
    DESTINATION rmtpserver/data/mediamtx)
install(FILES install.cmd install.ps1 README.md DESTINATION .)
