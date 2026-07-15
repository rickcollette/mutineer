cmake_minimum_required(VERSION 3.16)
project(mutineer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(MUTINEER_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" OFF)
if (MUTINEER_WARNINGS_AS_ERRORS)
  set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
endif()

add_executable(mutineer
  src/main.c
  src/config.c
  src/auth.c
  src/util.c
  src/process.c
  src/archive_util.c
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
  src/console_service.c
  src/scheduler.c
  src/doors.c
  src/file_cmds.c
  src/msg_cmds.c
  src/fsedit.c
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

find_library(LIBARCHIVE_LIBRARY
  NAMES archive libarchive.so.13
  PATHS /usr/lib /usr/lib/x86_64-linux-gnu /lib /lib/x86_64-linux-gnu
  REQUIRED)
target_link_libraries(mutineer PRIVATE ${LIBARCHIVE_LIBRARY})

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

# ============================================================================
# Buccaneer interpreted language runtime
# ============================================================================
# Built as an internal static library for BBS door/addon execution.

set(BUCCANEER_SOURCES
  src/buccaneer/value.c
  src/buccaneer/diag.c
  src/buccaneer/lexer.c
  src/buccaneer/ast.c
  src/buccaneer/parser.c
  src/buccaneer/semantic.c
  src/buccaneer/emit.c
  src/buccaneer/module.c
  src/buccaneer/vm.c
  src/buccaneer/host.c
  src/buccaneer/package.c
  src/buccaneer/bbs.c
)

add_library(buccaneer STATIC ${BUCCANEER_SOURCES})
target_include_directories(buccaneer PUBLIC src/buccaneer)
target_compile_options(buccaneer PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(buccaneer PUBLIC m)

target_include_directories(mutineer PRIVATE src/buccaneer)
target_link_libraries(mutineer PRIVATE buccaneer)

add_executable(test_buccaneer_host tests/test_buccaneer_host.c)
target_include_directories(test_buccaneer_host PRIVATE src/buccaneer)
target_link_libraries(test_buccaneer_host PRIVATE buccaneer)
add_test(NAME buccaneer_host COMMAND test_buccaneer_host)

add_executable(test_buccaneer_vm tests/test_buccaneer_vm.c)
target_include_directories(test_buccaneer_vm PRIVATE src/buccaneer)
target_link_libraries(test_buccaneer_vm PRIVATE buccaneer)
add_test(NAME buccaneer_vm COMMAND test_buccaneer_vm)

add_executable(test_buccaneer_bbs tests/test_buccaneer_bbs.c)
target_include_directories(test_buccaneer_bbs PRIVATE src/buccaneer)
target_link_libraries(test_buccaneer_bbs PRIVATE buccaneer)
add_test(NAME buccaneer_bbs COMMAND test_buccaneer_bbs)

add_executable(test_scheduler
  tests/test_scheduler.c
  src/scheduler.c
  src/process.c
  src/acs.c
  src/log.c
  src/db.c
  src/hash.c
  src/util.c
)
target_include_directories(test_scheduler PRIVATE include)
target_link_libraries(test_scheduler PRIVATE OpenSSL::Crypto Threads::Threads)
target_require_sqlite(test_scheduler)
add_test(NAME scheduler COMMAND test_scheduler WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_acs tests/test_acs.c src/acs.c src/util.c src/db.c src/hash.c)
target_include_directories(test_acs PRIVATE include)
target_link_libraries(test_acs PRIVATE OpenSSL::Crypto)
target_require_sqlite(test_acs)
add_test(NAME acs COMMAND test_acs)

add_executable(test_doors
  tests/test_doors.c
  src/doors.c
  src/process.c
  src/util.c
  src/log.c
  src/db.c
  src/hash.c
  src/config.c
  src/acs.c
)
target_include_directories(test_doors PRIVATE include)
target_include_directories(test_doors PRIVATE src/buccaneer)
target_link_libraries(test_doors PRIVATE OpenSSL::Crypto Threads::Threads)
target_link_libraries(test_doors PRIVATE buccaneer)
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

add_executable(test_file_cmds tests/test_file_cmds.c src/archive_util.c src/util.c)
target_include_directories(test_file_cmds PRIVATE include)
target_link_libraries(test_file_cmds PRIVATE ${LIBARCHIVE_LIBRARY})
add_test(NAME file_cmds COMMAND test_file_cmds WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_process_supervision tests/test_process_supervision.c src/process.c src/log.c)
target_include_directories(test_process_supervision PRIVATE include)
target_link_libraries(test_process_supervision PRIVATE Threads::Threads)
add_test(NAME process_supervision COMMAND test_process_supervision)

add_executable(test_telnet_password tests/test_telnet_password.c src/telnet.c src/util.c)
target_include_directories(test_telnet_password PRIVATE include)
add_test(NAME telnet_password COMMAND test_telnet_password)

add_executable(test_string_builders tests/test_string_builders.c src/util.c)
target_include_directories(test_string_builders PRIVATE include)
add_test(NAME string_builders COMMAND test_string_builders WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_temp_paths tests/test_temp_paths.c)
add_test(NAME temp_paths COMMAND test_temp_paths WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_first15_matrix tests/test_first15_matrix.c)
target_include_directories(test_first15_matrix PRIVATE include)
add_test(NAME first15_matrix COMMAND test_first15_matrix WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

foreach(next15_test
  test_config_editor
  test_menu_flags
  test_audit_runtime
  test_who_guest_maintenance
  test_scheduler_wfc_lifecycle
  test_backup
  test_file_validation
  test_start_menu
  test_plank_bbs_integration
  test_runtime_stress
)
  add_executable(${next15_test} tests/test_next15_matrix.c src/config.c src/util.c)
  target_include_directories(${next15_test} PRIVATE include)
  target_compile_options(${next15_test} PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME ${next15_test} COMMAND ${next15_test} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()
set_tests_properties(test_runtime_stress PROPERTIES LABELS "sanitizer;stress")

add_test(NAME docker_quickstart_smoke
         COMMAND ${CMAKE_SOURCE_DIR}/tests/docker_quickstart_smoke.sh ${CMAKE_BINARY_DIR}
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(docker_quickstart_smoke PROPERTIES LABELS "docker")

add_executable(test_plank_temp_policy tests/test_plank_temp_policy.c)
target_compile_options(test_plank_temp_policy PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME plank_temp_policy COMMAND test_plank_temp_policy WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME plank_temp_policy_tmpdir
         COMMAND ${CMAKE_COMMAND} -E env TMPDIR=${CMAKE_BINARY_DIR}/tmp $<TARGET_FILE:test_plank_temp_policy>
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_docs_consistency tests/test_docs_consistency.c)
target_compile_options(test_docs_consistency PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME docs_consistency COMMAND test_docs_consistency WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
find_program(PYTHON3_EXECUTABLE python3)
if(PYTHON3_EXECUTABLE)
  add_test(NAME website_renderer
    COMMAND ${PYTHON3_EXECUTABLE} scripts/build-website.py --self-test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME website_generation
    COMMAND ${PYTHON3_EXECUTABLE} scripts/build-website.py --check-or-temp-output
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

add_executable(test_fsedit tests/test_fsedit.c src/fsedit.c)
target_include_directories(test_fsedit PRIVATE include)
target_link_libraries(test_fsedit PRIVATE Threads::Threads)
add_test(NAME fsedit COMMAND test_fsedit)

# CLI tests for tools (shell script)
add_test(NAME tools_cli
         COMMAND ${CMAKE_SOURCE_DIR}/tests/test_tools_cli.sh ${CMAKE_BINARY_DIR}
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(tools_cli PROPERTIES
  DEPENDS tools
  LABELS "cli"
)

# Expect test suite (interactive tests)
find_program(EXPECT_EXECUTABLE expect)
if(EXPECT_EXECUTABLE)
  add_test(NAME expect_suite 
           COMMAND ${CMAKE_SOURCE_DIR}/tests/run_expect_tests.sh ${CMAKE_BINARY_DIR}
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
  target_link_libraries(${name} PRIVATE OpenSSL::Crypto ${LIBARCHIVE_LIBRARY})
endfunction()

# Build all tools
add_mutineer_tool(mutineer-qwkgen)
target_sources(mutineer-qwkgen PRIVATE src/archive_util.c)
target_link_libraries(mutineer-qwkgen PRIVATE ${LIBARCHIVE_LIBRARY})
add_mutineer_tool(mutineer-msgpack)
add_mutineer_tool(mutineer-userpack)
add_mutineer_tool(mutineer-filepack)
add_mutineer_tool(mutineer-stats)
add_mutineer_tool(mutineer-maint)
add_mutineer_tool(mutineer-initbbs)
add_mutineer_tool(mutineer-netmail-export)

add_executable(mutineer-console
  src/tools/mutineer-console.c
  src/config.c
  src/util.c
)
target_include_directories(mutineer-console PRIVATE include src/tools)
target_compile_options(mutineer-console PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME console_protocol
         COMMAND ${CMAKE_SOURCE_DIR}/tests/test_console_protocol.sh ${CMAKE_BINARY_DIR}
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME cove_hub_smoke
         COMMAND ${CMAKE_SOURCE_DIR}/tests/test_cove_hub.sh ${CMAKE_BINARY_DIR}
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME release_scripts_smoke
         COMMAND ${CMAKE_SOURCE_DIR}/tests/test_release_scripts.sh ${CMAKE_BINARY_DIR}
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

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
  mutineer-console
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
add_test(NAME plugin COMMAND test_plugin ${CMAKE_BINARY_DIR}/plugins/hello.so WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(plugin PROPERTIES DEPENDS "hello;chat_plugin")

add_executable(bucc
  src/buccaneer/tools/bucc.c
  src/buccaneer/tools/formatter.c
  src/buccaneer/tools/linter.c
  src/buccaneer/tools/simulator.c
)
target_include_directories(bucc PRIVATE src/buccaneer)
target_link_libraries(bucc PRIVATE buccaneer)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tmp)
add_test(NAME bucc_run_temp_fixture
         COMMAND ${CMAKE_COMMAND} -E env TMPDIR=${CMAKE_BINARY_DIR}/tmp $<TARGET_FILE:bucc> run ${CMAKE_SOURCE_DIR}/tests/fixtures/temp_run.bucc
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(bucc_run_temp_fixture PROPERTIES DEPENDS bucc)
