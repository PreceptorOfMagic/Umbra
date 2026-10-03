# windows specific packaging
install(TARGETS sunshine RUNTIME DESTINATION "." COMPONENT application)

# Hardening: include zlib1.dll (loaded via LoadLibrary() in openssl's libcrypto.a)
install(FILES "${ZLIB}" DESTINATION "." COMPONENT application)

# ViGEmBus installer
set(VIGEMBUS_INSTALLER "${CMAKE_BINARY_DIR}/vigembus_installer.exe")
file(DOWNLOAD
        "https://github.com/nefarius/ViGEmBus/releases/download/v1.21.442.0/ViGEmBus_1.21.442_x64_x86_arm64.exe"
        ${VIGEMBUS_INSTALLER}
        SHOW_PROGRESS
        EXPECTED_HASH SHA256=155c50f1eec07bdc28d2f61a3e3c2c6c132fee7328412de224695f89143316bc
        TIMEOUT 60
)
install(FILES ${VIGEMBUS_INSTALLER}
        DESTINATION "scripts"
        RENAME "vigembus_installer.exe"
        COMPONENT gamepad)

# Adding tools
# Aurora Link ships next to sunshine.exe, in the SAME component as the
# application, so it is installed and uninstalled with Umbra -- the whole point
# of moving it out of the hand-built script tree.
install(TARGETS aurora-link RUNTIME DESTINATION "." COMPONENT application)

# Umbra: the GPU HEVC co-op encoder, so NVIDIA+AMD and AMD+AMD co-op work from a FRESH install.
# Until 2026-10-02 it reached a host only through a separate hand-run payload script, so a host
# installed from this package had none and those pairings could not start. Built from audited
# source -- Eclipse repo experimental/gpu-hevc (GPL-3.0-only), configured with -DBUILD_HOST_DLLS=ON.
# UMBRA_GPU_HEVC_DIR must hold aurora_gpu_hevc8.dll, aurora_gpu_hevc10.dll and kernels/; they land
# in gpu-hevc\ beside sunshine.exe, which is where gpu_hevc_device.inc loads them and what
# serverinfo UmbraCaps bit 3 (proc::gpu_hevc_encoder_installed) checks.
set(UMBRA_GPU_HEVC_DIR "" CACHE PATH "Folder with the built GPU HEVC co-op encoder (DLLs + kernels/)")
if(UMBRA_GPU_HEVC_DIR)
  foreach(required aurora_gpu_hevc8.dll aurora_gpu_hevc10.dll kernels)
    if(NOT EXISTS "${UMBRA_GPU_HEVC_DIR}/${required}")
      message(FATAL_ERROR "UMBRA_GPU_HEVC_DIR=${UMBRA_GPU_HEVC_DIR} has no ${required}")
    endif()
  endforeach()
  install(FILES "${UMBRA_GPU_HEVC_DIR}/aurora_gpu_hevc8.dll" "${UMBRA_GPU_HEVC_DIR}/aurora_gpu_hevc10.dll"
          DESTINATION "gpu-hevc" COMPONENT application)
  install(DIRECTORY "${UMBRA_GPU_HEVC_DIR}/kernels" DESTINATION "gpu-hevc" COMPONENT application)
else()
  message(WARNING "UMBRA_GPU_HEVC_DIR is not set: this package has NO GPU HEVC co-op encoder, so hosts "
                  "installed from it report UmbraCaps without bit 3 and Eclipse will not start "
                  "NVIDIA+AMD or AMD+AMD co-op on them. Set it for any release package.")
endif()

install(TARGETS dxgi-info RUNTIME DESTINATION "tools" COMPONENT dxgi)
install(TARGETS audio-info RUNTIME DESTINATION "tools" COMPONENT audio)

# Mandatory tools
install(TARGETS sunshinesvc RUNTIME DESTINATION "tools" COMPONENT application)

# Drivers
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/drivers/sudovda"
        DESTINATION "drivers"
        COMPONENT sudovda)

# Mandatory scripts
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/service/"
        DESTINATION "scripts"
        COMPONENT assets)
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/migration/"
        DESTINATION "scripts"
        COMPONENT assets)
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/path/"
        DESTINATION "scripts"
        COMPONENT assets)

# Configurable options for the service
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/autostart/"
        DESTINATION "scripts"
        COMPONENT autostart)

# scripts
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/firewall/"
        DESTINATION "scripts"
        COMPONENT firewall)
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/gamepad/"
        DESTINATION "scripts"
        COMPONENT gamepad)

# Sunshine assets
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/assets/"
        DESTINATION "${SUNSHINE_ASSETS_DIR}"
        COMPONENT assets)

# copy assets (excluding shaders) to build directory, for running without install
file(COPY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/assets/"
        DESTINATION "${CMAKE_BINARY_DIR}/assets"
        PATTERN "shaders" EXCLUDE)
# use junction for shaders directory
cmake_path(CONVERT "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/assets/shaders"
        TO_NATIVE_PATH_LIST shaders_in_build_src_native)
cmake_path(CONVERT "${CMAKE_BINARY_DIR}/assets/shaders" TO_NATIVE_PATH_LIST shaders_in_build_dest_native)
execute_process(COMMAND cmd.exe /c mklink /J "${shaders_in_build_dest_native}" "${shaders_in_build_src_native}")

set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}\\\\apollo.ico")

# The name of the directory that will be created in C:/Program files/
# Umbra: upgrade an existing Apollo install IN PLACE. The folder and the registry key stay
# "Apollo" -- the service, config\ (pairings, credentials, apps) and the installer's
# uninstall-before-install lookup all live under them. Derived from the project name (Umbra),
# a new install would land in Program Files\Umbra beside the old one and repoint ApolloService
# at an empty config. Only the name people see is Umbra.
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Apollo")
set(CPACK_NSIS_DISPLAY_NAME "Umbra")

# Setting components groups and dependencies
set(CPACK_COMPONENT_GROUP_CORE_EXPANDED true)

# sunshine binary
set(CPACK_COMPONENT_APPLICATION_DISPLAY_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_COMPONENT_APPLICATION_DESCRIPTION "${CMAKE_PROJECT_NAME} main application and required components.")
set(CPACK_COMPONENT_APPLICATION_GROUP "Core")
set(CPACK_COMPONENT_APPLICATION_REQUIRED true)
set(CPACK_COMPONENT_APPLICATION_DEPENDS assets)

# service auto-start script
set(CPACK_COMPONENT_AUTOSTART_DISPLAY_NAME "Launch on Startup")
set(CPACK_COMPONENT_AUTOSTART_DESCRIPTION "If enabled, launches Umbra automatically on system startup.")
set(CPACK_COMPONENT_AUTOSTART_GROUP "Core")

# assets
set(CPACK_COMPONENT_ASSETS_DISPLAY_NAME "Required Assets")
set(CPACK_COMPONENT_ASSETS_DESCRIPTION "Shaders, default box art, and web UI.")
set(CPACK_COMPONENT_ASSETS_GROUP "Core")
set(CPACK_COMPONENT_ASSETS_REQUIRED true)

# drivers
set(CPACK_COMPONENT_SUDOVDA_DISPLAY_NAME "SudoVDA")
set(CPACK_COMPONENT_SUDOVDA_DESCRIPTION "Driver required for Virtual Display to function.")
set(CPACK_COMPONENT_SUDOVDA_GROUP "Drivers")
set(CPACK_COMPONENT_SUDOVDA_REQUIRED true)

# audio tool
set(CPACK_COMPONENT_AUDIO_DISPLAY_NAME "audio-info")
set(CPACK_COMPONENT_AUDIO_DESCRIPTION "CLI tool providing information about sound devices.")
set(CPACK_COMPONENT_AUDIO_GROUP "Tools")

# display tool
set(CPACK_COMPONENT_DXGI_DISPLAY_NAME "dxgi-info")
set(CPACK_COMPONENT_DXGI_DESCRIPTION "CLI tool providing information about graphics cards and displays.")
set(CPACK_COMPONENT_DXGI_GROUP "Tools")

# firewall scripts
set(CPACK_COMPONENT_FIREWALL_DISPLAY_NAME "Add Firewall Exclusions")
set(CPACK_COMPONENT_FIREWALL_DESCRIPTION "Scripts to enable or disable firewall rules.")
set(CPACK_COMPONENT_FIREWALL_GROUP "Scripts")

# gamepad scripts
set(CPACK_COMPONENT_GAMEPAD_DISPLAY_NAME "Virtual Gamepad")
set(CPACK_COMPONENT_GAMEPAD_DESCRIPTION "Scripts to install and uninstall Virtual Gamepad.")
set(CPACK_COMPONENT_GAMEPAD_GROUP "Scripts")

# include specific packaging
include(${CMAKE_MODULE_PATH}/packaging/windows_nsis.cmake)
include(${CMAKE_MODULE_PATH}/packaging/windows_wix.cmake)
