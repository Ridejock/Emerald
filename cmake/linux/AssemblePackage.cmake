# Assemble dist/<NAME>/ and dist/<NAME>.tar.gz. Invoked at build time:
#   cmake -DSOURCE_BIN=... -DASSETS_DIR=... -DSHADERS_DIR=... -DLAUNCHER=... -DREADME=...
#         -DOUTDIR=... -DNAME=... -P AssemblePackage.cmake
if(NOT SOURCE_BIN OR NOT ASSETS_DIR OR NOT SHADERS_DIR OR NOT LAUNCHER OR NOT README
   OR NOT OUTDIR OR NOT NAME)
    message(FATAL_ERROR "AssemblePackage.cmake: missing -D arguments")
endif()

set(stage "${OUTDIR}/${NAME}")
file(REMOVE_RECURSE "${stage}")
file(MAKE_DIRECTORY "${stage}/lib")
file(COPY "${SOURCE_BIN}" DESTINATION "${stage}")
file(COPY "${ASSETS_DIR}" DESTINATION "${stage}")
file(COPY "${SHADERS_DIR}" DESTINATION "${stage}")
file(COPY "${LAUNCHER}" DESTINATION "${stage}")
file(COPY "${README}" DESTINATION "${stage}")
file(WRITE "${stage}/lib/README.txt"
     "Shared libraries placed here are found via RPATH (\$ORIGIN/lib).\n"
     "SDL3 is linked statically and is not shipped as its own library.\n")

file(CHMOD "${stage}/Sandbox" "${stage}/Sandbox.sh"
     FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
                      WORLD_READ WORLD_EXECUTE)

execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar czf "${NAME}.tar.gz" --format=gnutar "${NAME}"
    WORKING_DIRECTORY "${OUTDIR}"
    COMMAND_ERROR_IS_FATAL ANY)
