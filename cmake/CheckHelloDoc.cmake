# Fails when docs/hello-world.md no longer contains examples/hello/main.cpp verbatim.
# Line endings are normalized so a Windows checkout still matches.
if(NOT HELLO_SOURCE OR NOT HELLO_DOC)
    message(FATAL_ERROR "CheckHelloDoc: set -DHELLO_SOURCE and -DHELLO_DOC")
endif()

file(READ "${HELLO_SOURCE}" source)
file(READ "${HELLO_DOC}" doc)
string(REPLACE "\r\n" "\n" source "${source}")
string(REPLACE "\r\n" "\n" doc "${doc}")

string(FIND "${doc}" "${source}" pos)
if(pos LESS 0)
    message(FATAL_ERROR
        "docs/hello-world.md must contain examples/hello/main.cpp verbatim. "
        "The fenced listing and the file have diverged.")
endif()
