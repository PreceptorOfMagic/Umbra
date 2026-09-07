cmake_minimum_required(VERSION 3.20)

project(sunshine_tools)

include_directories(${CMAKE_SOURCE_DIR})

add_executable(dxgi-info dxgi.cpp)
set_target_properties(dxgi-info PROPERTIES CXX_STANDARD 23)
target_link_libraries(dxgi-info
        ${CMAKE_THREAD_LIBS_INIT}
        dxgi
        ${PLATFORM_LIBRARIES})
target_compile_options(dxgi-info PRIVATE ${SUNSHINE_COMPILE_OPTIONS})

add_executable(audio-info audio.cpp utils.cpp)
set_target_properties(audio-info PROPERTIES CXX_STANDARD 23)
target_link_libraries(audio-info
        ${Boost_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ksuser
        ${PLATFORM_LIBRARIES})
target_compile_options(audio-info PRIVATE ${SUNSHINE_COMPILE_OPTIONS})

add_executable(sunshinesvc sunshinesvc.cpp)
set_target_properties(sunshinesvc PROPERTIES CXX_STANDARD 23)
target_link_libraries(sunshinesvc
        ${CMAKE_THREAD_LIBS_INIT}
        wtsapi32
        ${PLATFORM_LIBRARIES})
target_compile_options(sunshinesvc PRIVATE ${SUNSHINE_COMPILE_OPTIONS})

# Aurora Link — host companion (pad bridge, audio-cable visibility, agent gate).
# Built and installed with Apollo so `uninstall Apollo` removes it too, and a
# second host gets it by installing Apollo rather than by copying a hand-built
# tree of scripts. ViGEmClient is the same vendored copy sunshine itself uses.
add_executable(aurora-link
        ${CMAKE_SOURCE_DIR}/src/aurora_link/main.cpp
        ${CMAKE_SOURCE_DIR}/src/aurora_link/cable.cpp
        ${CMAKE_SOURCE_DIR}/src/aurora_link/pad.cpp
        ${CMAKE_SOURCE_DIR}/src/aurora_link/rename.cpp
        ${CMAKE_SOURCE_DIR}/src/aurora_link/mic.cpp
        ${CMAKE_SOURCE_DIR}/src/aurora_link/audio_default.cpp
        ${CMAKE_SOURCE_DIR}/src/aurora_link/session.cpp
        ${CMAKE_SOURCE_DIR}/third-party/ViGEmClient/src/ViGEmClient.cpp)
set_target_properties(aurora-link PROPERTIES CXX_STANDARD 23)
target_include_directories(aurora-link PRIVATE
        ${CMAKE_SOURCE_DIR}/src/aurora_link
        ${CMAKE_SOURCE_DIR}/third-party/ViGEmClient/include)
target_compile_definitions(aurora-link PRIVATE ERROR_INVALID_DEVICE_OBJECT_PARAMETER=650L)
target_link_libraries(aurora-link
        ${CMAKE_THREAD_LIBS_INIT}
        setupapi
        ws2_32
        ole32
        propsys
        shlwapi
        opus       # already a dependency of sunshine itself; the mic host decodes with it
        ${PLATFORM_LIBRARIES})
target_compile_options(aurora-link PRIVATE ${SUNSHINE_COMPILE_OPTIONS})
