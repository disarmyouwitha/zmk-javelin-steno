set(JAVELIN_TEST_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH
    "Repository with the pinned fixture compiler and fixture JSON")
set(JAVELIN_ENGINE "${ZEPHYR_BASE}/../javelin-steno" CACHE PATH
    "Pinned Javelin source checkout")

# The integration's Zephyr preparation removes unused desktop unit tests.
# Prepare a private copy, leaving the pinned source checkout intact.
set(engine "${CMAKE_CURRENT_BINARY_DIR}/javelin-engine")
file(COPY "${JAVELIN_ENGINE}/" DESTINATION "${engine}" PATTERN ".git" EXCLUDE)
execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" "${JAVELIN_TEST_REPO}/tools/build/prepare_engine.py" "${engine}"
  COMMAND_ERROR_IS_FATAL ANY
)
set(xip_patch "${CMAKE_CURRENT_SOURCE_DIR}/src/prepare_xip_reader.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${xip_patch}")
execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" "${xip_patch}"
          "${engine}/dictionary/compact_map_dictionary.cc"
  COMMAND_ERROR_IS_FATAL ANY
)

set(dictionary_bin "${CMAKE_CURRENT_BINARY_DIR}/external-cat-test.bin")
set(dictionary_compiler "${JAVELIN_TEST_REPO}/tools/dict_compiler/compile_dict.py")
set(dictionary_json "${JAVELIN_TEST_REPO}/dicts/cat-test.json")
add_custom_command(
  OUTPUT "${dictionary_bin}"
  COMMAND "${PYTHON_EXECUTABLE}" "${dictionary_compiler}"
          "${dictionary_json}" --base-addr 0x12fff000 -o "${dictionary_bin}"
  DEPENDS "${dictionary_compiler}" "${dictionary_json}"
  VERBATIM
)
set_source_files_properties(src/javelin_fixture.S PROPERTIES OBJECT_DEPENDS "${dictionary_bin}")

target_sources(app PRIVATE
  src/javelin_fixture.S
  src/javelin_lookup.cc
  "${engine}/dictionary/compact_map_dictionary.cc"
  "${engine}/dictionary/dictionary.cc"
  "${engine}/container/list.cc"
  "${engine}/bit.cc"
  "${engine}/crc32.cc"
  "${engine}/mem.cc"
  "${engine}/str.cc"
  "${engine}/stroke.cc"
)
target_include_directories(app PRIVATE "${engine}" src)
target_compile_definitions(app PRIVATE
  JAVELIN_PROBE=1
  JAVELIN_FIXTURE_PATH="${dictionary_bin}"
)
