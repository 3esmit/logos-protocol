# check_qtfree_plain.cmake — CI guard for the Qt-free plain-transport path.
#
# The files below implement the loop-free, Qt-free runtime path (provider
# dispatch, plain consumer, wire servers/codecs). They must NOT pull in the
# QVariant boundary (qvariant_rpc_value.h) or QDebug — doing so silently
# re-couples the Qt-free path to Qt and defeats the whole point (a pure-lp_*
# SDK / node process consuming/serving modules with no Qt runtime).
#
# We match on #include lines only (via file(STRINGS ... REGEX)), so a comment
# that merely mentions QVariant/QDebug does not trip the guard.
#
# Run with: cmake -DSRC_DIR=<repo>/cpp -P check_qtfree_plain.cmake

set(QTFREE_FILES
    implementations/plain/rpc_server.cpp
    implementations/plain/rpc_server.h
    implementations/plain/rpc_connection.h
    implementations/plain/json_mapping.cpp
    implementations/plain/json_mapping.h
    implementations/plain/rpc_framing.cpp
    implementations/plain/json_codec.cpp
    implementations/plain/cbor_codec.cpp
    implementations/plain/io_context_pool.cpp
    lp_provider_dispatch.cpp
    lp_provider_dispatch.h
    lp_plain_client.cpp
    lp_plain_client.h
)

set(bad "")
foreach(rel IN LISTS QTFREE_FILES)
    set(f "${SRC_DIR}/${rel}")
    if(EXISTS "${f}")
        file(STRINGS "${f}" hits
             REGEX "^[ \t]*#[ \t]*include.*(qvariant_rpc_value|QDebug)")
        if(hits)
            list(APPEND bad "  ${rel}: ${hits}")
        endif()
    else()
        list(APPEND bad "  ${rel}: MISSING (update this guard's file list)")
    endif()
endforeach()

if(bad)
    string(REPLACE ";" "\n" bad "${bad}")
    message(FATAL_ERROR
        "Qt-free plain path re-coupled to Qt (QVariant/QDebug include found):\n${bad}")
endif()

message(STATUS "qtfree-plain-path guard: OK — no QVariant/QDebug coupling")
