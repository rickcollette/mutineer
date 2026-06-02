cmake_minimum_required(VERSION 3.16)
project(mutineer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(mutineer
  src/main.c
  src/config.c
  src/util.c
  src/net_listener.c
  src/session.c
  src/telnet.c
  src/menu.c
  src/menu_template.c
  src/db.c
  src/log.c
  src/hash.c
  src/acs.c
  src/mci.c
  src/wfc.c
  src/scheduler.c
  src/doors.c
  src/file_cmds.c
  src/msg_cmds.c
  src/chat.c
  src/qwk.c
  src/fido_netmail.c
  src/maint.c
  src/startup.c
  src/plugin_loader.c
  src/plugin_registry.c
  src/plugin_host_api.c
)

target_include_directories(mutineer PRIVATE include)

target_compile_options(mutineer PRIVATE -Wall -Wextra -Wpedantic)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if (ENABLE_ASAN)
  target_compile_options(mutineer PRIVATE -fsanitize=address)
  target_link_options(mutineer PRIVATE -fsanitize=address)
endif()

# SQLite is required. Mutineer does not support a non-persistent DB mode.
find_package(SQLite3 REQUIRED)

function(target_require_sqlite target)
  target_compile_definitions(${target} PRIVATE HAVE_SQLITE=1)
  target_include_directories(${target} PRIVATE ${SQLite3_INCLUDE_DIRS})
  target_link_libraries(${target} PRIVATE ${SQLite3_LIBRARIES})
endfunction()

target_require_sqlite(mutineer)

# pthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(mutineer PRIVATE Threads::Threads)

find_package(OpenSSL REQUIRED)
target_link_libraries(mutineer PRIVATE OpenSSL::Crypto)

# Argon2 (optional)
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
  pkg_check_modules(ARGON2 libargon2)
  if (ARGON2_FOUND)
    target_compile_definitions(mutineer PRIVATE HAVE_ARGON2=1)
    target_link_libraries(mutineer PRIVATE ${ARGON2_LIBRARIES})
    target_include_directories(mutineer PRIVATE ${ARGON2_INCLUDE_DIRS})
  endif()
endif()

# crypt (for bcrypt via system crypt)
include(CheckLibraryExists)
check_library_exists(crypt crypt "" HAVE_LIBCRYPT)
if (HAVE_LIBCRYPT)
  target_compile_definitions(mutineer PRIVATE HAVE_CRYPT=1)
  target_link_libraries(mutineer PRIVATE crypt)
endif()

# dl (for plugin loading via dlopen)
target_link_libraries(mutineer PRIVATE ${CMAKE_DL_LIBS})

enable_testing()
add_executable(test_acs tests/test_acs.c src/acs.c src/util.c src/db.c src/hash.c)
target_include_directories(test_acs PRIVATE include)
target_link_libraries(test_acs PRIVATE OpenSSL::Crypto)
target_require_sqlite(test_acs)
add_test(NAME acs COMMAND test_acs)

add_executable(test_doors
  tests/test_doors.c
  src/doors.c
  src/util.c
  src/log.c
  src/db.c
  src/hash.c
  src/config.c
  src/acs.c
)
target_include_directories(test_doors PRIVATE include)
target_link_libraries(test_doors PRIVATE OpenSSL::Crypto Threads::Threads)
target_require_sqlite(test_doors)
add_test(NAME doors COMMAND test_doors WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_mci tests/test_mci.c src/mci.c src/util.c src/db.c src/hash.c src/acs.c)
target_include_directories(test_mci PRIVATE include)
target_link_libraries(test_mci PRIVATE OpenSSL::Crypto)
target_require_sqlite(test_mci)
add_test(NAME mci COMMAND test_mci)

add_executable(test_menu tests/test_menu.c src/menu.c src/mci.c src/util.c src/db.c src/hash.c src/acs.c)
target_include_directories(test_menu PRIVATE include)
target_link_libraries(test_menu PRIVATE OpenSSL::Crypto)
target_require_sqlite(test_menu)
add_test(NAME menu COMMAND test_menu WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_menu_template tests/test_menu_template.c src/menu_template.c src/menu.c src/mci.c src/util.c src/db.c src/hash.c src/acs.c src/log.c)
target_include_directories(test_menu_template PRIVATE include)
target_link_libraries(test_menu_template PRIVATE OpenSSL::Crypto)
target_require_sqlite(test_menu_template)
add_test(NAME menu_template COMMAND test_menu_template WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_smoke tests/test_smoke.c src/menu.c src/mci.c src/util.c src/db.c src/hash.c src/acs.c)
target_include_directories(test_smoke PRIVATE include)
target_link_libraries(test_smoke PRIVATE OpenSSL::Crypto)
target_require_sqlite(test_smoke)
add_test(NAME smoke COMMAND test_smoke WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Tool tests
add_executable(test_tools tests/test_tools.c src/db.c src/config.c src/util.c src/acs.c src/hash.c src/fido_netmail.c)
target_include_directories(test_tools PRIVATE include)
target_require_sqlite(test_tools)
target_link_libraries(test_tools PRIVATE OpenSSL::Crypto)
add_test(NAME tools COMMAND test_tools WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_file_cmds tests/test_file_cmds.c)
target_include_directories(test_file_cmds PRIVATE include)
add_test(NAME file_cmds COMMAND test_file_cmds WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# CLI tests for tools (shell script)
add_test(NAME tools_cli
         COMMAND ${CMAKE_SOURCE_DIR}/tests/test_tools_cli.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(tools_cli PROPERTIES
  DEPENDS tools
  LABELS "cli"
)

# Expect test suite (interactive tests)
find_program(EXPECT_EXECUTABLE expect)
if(EXPECT_EXECUTABLE)
  add_test(NAME expect_suite 
           COMMAND ${CMAKE_SOURCE_DIR}/tests/run_expect_tests.sh
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(expect_suite PROPERTIES 
    TIMEOUT 300
    LABELS "interactive"
  )
endif()

# ============================================================================
# Standalone Tools
# ============================================================================
# These utilities can be run from command line, cron, or the BBS scheduler.
# They share the database and config code but run independently of the BBS.

set(TOOL_COMMON_SOURCES
  src/db.c
  src/config.c
  src/util.c
  src/acs.c
  src/hash.c
  src/log.c
  src/fido_netmail.c
)

# Helper function to create a tool executable
function(add_mutineer_tool name)
  add_executable(${name} src/tools/${name}.c ${TOOL_COMMON_SOURCES})
  target_include_directories(${name} PRIVATE include src/tools)
  target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)

  target_require_sqlite(${name})
  target_link_libraries(${name} PRIVATE OpenSSL::Crypto)
endfunction()

# Build all tools
add_mutineer_tool(mutineer-qwkgen)
add_mutineer_tool(mutineer-msgpack)
add_mutineer_tool(mutineer-userpack)
add_mutineer_tool(mutineer-filepack)
add_mutineer_tool(mutineer-stats)
add_mutineer_tool(mutineer-maint)
add_mutineer_tool(mutineer-initbbs)
add_mutineer_tool(mutineer-netmail-export)

# mutineer-validate needs additional sources for menu/template validation
add_executable(mutineer-validate 
  src/tools/mutineer-validate.c 
  src/menu.c 
  src/menu_template.c 
  src/mci.c
  ${TOOL_COMMON_SOURCES}
)
target_include_directories(mutineer-validate PRIVATE include src/tools)
target_compile_options(mutineer-validate PRIVATE -Wall -Wextra -Wpedantic)
target_require_sqlite(mutineer-validate)
target_link_libraries(mutineer-validate PRIVATE OpenSSL::Crypto)

# Validation test using mutineer-validate
add_test(NAME validate_menus 
         COMMAND mutineer-validate menus
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Custom target to build all tools
add_custom_target(tools DEPENDS
  mutineer-qwkgen
  mutineer-msgpack
  mutineer-userpack
  mutineer-filepack
  mutineer-stats
  mutineer-maint
  mutineer-initbbs
  mutineer-netmail-export
  mutineer-validate
)

# ============================================================================
# PLANK/COVE Subsystem
# ============================================================================
# External utilities for PLANK network protocol and COVE hub functionality.
# These run outside the BBS core process and use the database directly.

# Check for optional ZSTD compression
pkg_check_modules(ZSTD libzstd)

# PLANK library sources
set(PLANK_SOURCES
  src/plank/plank_core.c
  src/plank/plank_cbor.c
  src/plank/plank_crypto.c
  src/plank/plank_object.c
  src/plank/plank_store.c
  src/plank/plank_bundle.c
  src/plank/plank_link.c
  src/plank/plank_route.c
  src/plank/plank_policy.c
)

# Helper function to create a PLANK tool executable
function(add_plank_tool name)
  add_executable(${name} src/tools/${name}.c ${TOOL_COMMON_SOURCES} ${PLANK_SOURCES})
  target_include_directories(${name} PRIVATE include src/tools)
  target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)

  target_require_sqlite(${name})
  target_link_libraries(${name} PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
  
  if(ZSTD_FOUND)
    target_compile_definitions(${name} PRIVATE HAVE_ZSTD=1)
    target_include_directories(${name} PRIVATE ${ZSTD_INCLUDE_DIRS})
    target_link_libraries(${name} PRIVATE ${ZSTD_LIBRARIES})
  endif()
endfunction()

# Build PLANK tools
add_plank_tool(plankd)
add_plank_tool(coved)
add_plank_tool(plankctl)
add_plank_tool(plankpack)
add_plank_tool(plank-offline)

# Custom target to build all PLANK tools
add_custom_target(plank DEPENDS
  plankd
  coved
  plankctl
  plankpack
  plank-offline
)

# PLANK tests
add_executable(test_plank_cbor tests/plank/test_cbor.c src/plank/plank_cbor.c src/plank/plank_core.c src/plank/plank_crypto.c)
target_include_directories(test_plank_cbor PRIVATE include)
target_compile_options(test_plank_cbor PRIVATE -Wall -Wextra)
target_link_libraries(test_plank_cbor PRIVATE OpenSSL::Crypto)
add_test(NAME plank_cbor COMMAND test_plank_cbor)

add_executable(test_plank_crypto tests/plank/test_crypto.c src/plank/plank_crypto.c src/plank/plank_core.c src/plank/plank_cbor.c)
target_include_directories(test_plank_crypto PRIVATE include)
target_compile_options(test_plank_crypto PRIVATE -Wall -Wextra)
target_link_libraries(test_plank_crypto PRIVATE OpenSSL::Crypto)
if(ZSTD_FOUND)
  target_compile_definitions(test_plank_crypto PRIVATE HAVE_ZSTD=1)
  target_link_libraries(test_plank_crypto PRIVATE ${ZSTD_LIBRARIES})
endif()
add_test(NAME plank_crypto COMMAND test_plank_crypto)

add_executable(test_plank_object tests/plank/test_object.c ${PLANK_SOURCES} ${TOOL_COMMON_SOURCES})
target_include_directories(test_plank_object PRIVATE include)
target_compile_options(test_plank_object PRIVATE -Wall -Wextra)
target_require_sqlite(test_plank_object)
target_link_libraries(test_plank_object PRIVATE OpenSSL::Crypto)
if(ZSTD_FOUND)
  target_compile_definitions(test_plank_object PRIVATE HAVE_ZSTD=1)
  target_link_libraries(test_plank_object PRIVATE ${ZSTD_LIBRARIES})
endif()
add_test(NAME plank_object COMMAND test_plank_object)

add_executable(test_plank_bundle tests/plank/test_bundle.c ${PLANK_SOURCES} ${TOOL_COMMON_SOURCES})
target_include_directories(test_plank_bundle PRIVATE include)
target_compile_options(test_plank_bundle PRIVATE -Wall -Wextra)
target_require_sqlite(test_plank_bundle)
target_link_libraries(test_plank_bundle PRIVATE OpenSSL::Crypto)
if(ZSTD_FOUND)
  target_compile_definitions(test_plank_bundle PRIVATE HAVE_ZSTD=1)
  target_link_libraries(test_plank_bundle PRIVATE ${ZSTD_LIBRARIES})
endif()
add_test(NAME plank_bundle COMMAND test_plank_bundle WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_plank_store tests/plank/test_store.c ${PLANK_SOURCES} ${TOOL_COMMON_SOURCES})
target_include_directories(test_plank_store PRIVATE include)
target_compile_options(test_plank_store PRIVATE -Wall -Wextra)
target_require_sqlite(test_plank_store)
target_link_libraries(test_plank_store PRIVATE OpenSSL::Crypto)
if(ZSTD_FOUND)
  target_compile_definitions(test_plank_store PRIVATE HAVE_ZSTD=1)
  target_link_libraries(test_plank_store PRIVATE ${ZSTD_LIBRARIES})
endif()
add_test(NAME plank_store COMMAND test_plank_store WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_plank_link tests/plank/test_link.c ${PLANK_SOURCES} ${TOOL_COMMON_SOURCES})
target_include_directories(test_plank_link PRIVATE include)
target_compile_options(test_plank_link PRIVATE -Wall -Wextra)
target_require_sqlite(test_plank_link)
target_link_libraries(test_plank_link PRIVATE OpenSSL::Crypto OpenSSL::SSL)
if(ZSTD_FOUND)
  target_compile_definitions(test_plank_link PRIVATE HAVE_ZSTD=1)
  target_link_libraries(test_plank_link PRIVATE ${ZSTD_LIBRARIES})
endif()
add_test(NAME plank_link COMMAND test_plank_link WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_plank_route tests/plank/test_route.c ${PLANK_SOURCES} ${TOOL_COMMON_SOURCES})
target_include_directories(test_plank_route PRIVATE include)
target_compile_options(test_plank_route PRIVATE -Wall -Wextra)
target_require_sqlite(test_plank_route)
target_link_libraries(test_plank_route PRIVATE OpenSSL::Crypto OpenSSL::SSL)
if(ZSTD_FOUND)
  target_compile_definitions(test_plank_route PRIVATE HAVE_ZSTD=1)
  target_link_libraries(test_plank_route PRIVATE ${ZSTD_LIBRARIES})
endif()
add_test(NAME plank_route COMMAND test_plank_route WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_plank_policy tests/plank/test_policy.c ${PLANK_SOURCES} ${TOOL_COMMON_SOURCES})
target_include_directories(test_plank_policy PRIVATE include)
target_compile_options(test_plank_policy PRIVATE -Wall -Wextra)
target_require_sqlite(test_plank_policy)
target_link_libraries(test_plank_policy PRIVATE OpenSSL::Crypto OpenSSL::SSL)
if(ZSTD_FOUND)
  target_compile_definitions(test_plank_policy PRIVATE HAVE_ZSTD=1)
  target_link_libraries(test_plank_policy PRIVATE ${ZSTD_LIBRARIES})
endif()
add_test(NAME plank_policy COMMAND test_plank_policy WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ============================================================================
# Plugins
# ============================================================================
# Sample plugins demonstrating the plugin API
# Each plugin lives in its own folder: plugins/<plugin-name>/

# Create plugins output directory
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

# Helper function to build a plugin from its folder
# Usage: add_mutineer_plugin(folder_name source_file output_name)
# e.g., add_mutineer_plugin(hello hello.c hello) -> plugins/hello/hello.c -> hello.so
function(add_mutineer_plugin folder source output)
  add_library(${output} SHARED plugins/${folder}/${source})
  target_include_directories(${output} PRIVATE include)
  target_compile_options(${output} PRIVATE -Wall -Wextra -Wpedantic -fPIC)
  target_compile_definitions(${output} PRIVATE BBS_PLUGIN_IMPL=1)
  set_target_properties(${output} PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
  )
endfunction()

# Build sample plugins
add_mutineer_plugin(hello hello.c hello)
add_mutineer_plugin(chat chat.c chat_plugin)

# Custom target to build all plugins
add_custom_target(plugins DEPENDS hello chat_plugin)

# Plugin unit tests
add_executable(test_plugin 
  tests/test_plugin.c 
  src/plugin_loader.c 
  src/plugin_registry.c 
  src/plugin_host_api.c
  src/util.c 
  src/log.c
  src/db.c
  src/hash.c
  src/acs.c
)
target_include_directories(test_plugin PRIVATE include)
target_compile_options(test_plugin PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(test_plugin PRIVATE ${CMAKE_DL_LIBS} Threads::Threads OpenSSL::Crypto)
target_require_sqlite(test_plugin)
add_test(NAME plugin COMMAND test_plugin WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(plugin PROPERTIES DEPENDS "hello;chat_plugin")
