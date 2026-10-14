# Linux release layout. SDL3 is already static (cmake/Dependencies.cmake). With
# EMERALD_LINUX_PACKAGE the Sandbox is linked so a copied tree runs without a system SDL:
#   - RPATH $ORIGIN and $ORIGIN/lib (the binary finds libraries next to it)
#   - libstdc++ and libgcc linked statically (the archive does not depend on the distro's)
#   - emerald_package writes dist/Sandbox-linux-x64.tar.gz
#       Sandbox.sh, Sandbox, assets/, shaders/, lib/, README.txt

option(EMERALD_LINUX_PACKAGE
    "Link the Sandbox for a self-contained Linux archive and add the emerald_package target" OFF)

if(NOT EMERALD_LINUX_PACKAGE)
    return()
endif()

if(NOT UNIX OR APPLE)
    message(FATAL_ERROR "EMERALD_LINUX_PACKAGE is supported on Linux only")
endif()
if(NOT TARGET Sandbox)
    message(FATAL_ERROR "EMERALD_LINUX_PACKAGE needs the Sandbox target (EMERALD_BUILD_SANDBOX)")
endif()

set_target_properties(Sandbox PROPERTIES
    BUILD_RPATH "\$ORIGIN;\$ORIGIN/lib"
    INSTALL_RPATH "\$ORIGIN;\$ORIGIN/lib"
    BUILD_WITH_INSTALL_RPATH TRUE
)
# The archive must run on SteamOS and on Ubuntu without the builder's libstdc++.so.
target_link_options(Sandbox PRIVATE "-static-libstdc++" "-static-libgcc")

set(EMERALD_LINUX_PACKAGE_NAME "Sandbox-linux-x64")
set(EMERALD_LINUX_PACKAGE_DIR "${CMAKE_BINARY_DIR}/dist")

add_custom_target(emerald_package
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_BIN=$<TARGET_FILE:Sandbox>
            -DASSETS_DIR=$<TARGET_FILE_DIR:Sandbox>/assets
            -DSHADERS_DIR=$<TARGET_FILE_DIR:Sandbox>/shaders
            -DLAUNCHER=${CMAKE_CURRENT_SOURCE_DIR}/cmake/linux/Sandbox.sh
            -DREADME=${CMAKE_CURRENT_SOURCE_DIR}/cmake/linux/README.txt
            -DOUTDIR=${EMERALD_LINUX_PACKAGE_DIR}
            -DNAME=${EMERALD_LINUX_PACKAGE_NAME}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/linux/AssemblePackage.cmake
    DEPENDS Sandbox
    COMMENT "Packaging ${EMERALD_LINUX_PACKAGE_NAME}.tar.gz"
    VERBATIM
)
