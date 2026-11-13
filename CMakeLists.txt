if(CONFIG_ZMK_ZEN_R4_SPLIT_STATUS)
  if(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    target_sources(app PRIVATE src/zen_r4/split_status_central.c)
  else()
    target_sources(app PRIVATE
      src/zen_r4/split_status_display.c
      src/zen_r4/display/portrait.c
      src/zen_r4/display/pet_widget.c
      src/zen_r4/display/screen.c
      src/zen_r4/display/practice_widget.c
    )
  endif()
endif()

if(CONFIG_ZMK_ZEN_R4_PRACTICE)
  target_sources(app PRIVATE src/behaviors/behavior_javelin_practice.c)
endif()

if(CONFIG_ZMK_JAVELIN_STENO)

  target_sources(app PRIVATE src/behaviors/behavior_tap_first.c)

  zephyr_include_directories(include)
  target_sources(app PRIVATE src/behaviors/behavior_javelin_steno.c)

  # Only compile on central side of split keyboards (or non-split)
  if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

    # Patch javelin source for Zephyr compatibility
    set(JAVELIN_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/javelin")
    if(NOT EXISTS "${JAVELIN_SRC_DIR}/engine.h")
      # When this repository is the build module, west checks the engine out
      # beside Zephyr in the isolated workspace rather than inside this repo.
      set(JAVELIN_SRC_DIR "${ZEPHYR_BASE}/../javelin-steno")
    endif()
    if(NOT EXISTS "${JAVELIN_SRC_DIR}/engine.h")
      message(FATAL_ERROR "Pinned Javelin engine not found; see config/west.yml")
    endif()
    # Patch a private copy so the pinned west checkout stays clean.
    set(JAVELIN_PRIVATE_DIR "${CMAKE_CURRENT_BINARY_DIR}/javelin-engine")
    file(COPY "${JAVELIN_SRC_DIR}/" DESTINATION "${JAVELIN_PRIVATE_DIR}" PATTERN ".git" EXCLUDE)
    set(JAVELIN_SRC_DIR "${JAVELIN_PRIVATE_DIR}")
    execute_process(
      COMMAND ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_LIST_DIR}/tools/build/prepare_engine.py"
              "${JAVELIN_SRC_DIR}"
      COMMAND_ERROR_IS_FATAL ANY
    )

    # Override dir MUST come before javelin/ so our unit_test.h is found first
    zephyr_include_directories(include/javelin_overrides)
    zephyr_include_directories(${JAVELIN_SRC_DIR})
    zephyr_include_directories(include)

    # Keep behavioral compatibility patches private to this build.
    set(JAVELIN_SEGMENT_BUILDER_SRC "${CMAKE_CURRENT_BINARY_DIR}/compat/segment_builder.cc")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/compat")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      "${JAVELIN_SRC_DIR}/segment_builder.cc"
      "${CMAKE_CURRENT_LIST_DIR}/tools/build/prepare_segment_builder.py")
    execute_process(
      COMMAND ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_LIST_DIR}/tools/build/prepare_segment_builder.py"
        "${JAVELIN_SRC_DIR}/segment_builder.cc" "${JAVELIN_SEGMENT_BUILDER_SRC}"
      COMMAND_ERROR_IS_FATAL ANY)

    set(JAVELIN_NORMAL_MODE_SRC "${CMAKE_CURRENT_BINARY_DIR}/compat/engine_normal_mode.cc")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      "${JAVELIN_SRC_DIR}/engine_normal_mode.cc"
      "${CMAKE_CURRENT_LIST_DIR}/tools/build/prepare_engine_normal_mode.py")
    execute_process(
      COMMAND ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_LIST_DIR}/tools/build/prepare_engine_normal_mode.py"
        "${JAVELIN_SRC_DIR}/engine_normal_mode.cc" "${JAVELIN_NORMAL_MODE_SRC}"
      COMMAND_ERROR_IS_FATAL ANY)

    set(JAVELIN_COMPACT_MAP_SRC "${JAVELIN_SRC_DIR}/dictionary/compact_map_dictionary.cc")
    if(CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING)
      if(NOT EXISTS "${ZMK_JAVELIN_LAPWING_IMAGE}")
        message(FATAL_ERROR "Set ZMK_JAVELIN_LAPWING_IMAGE to the mapped Lapwing JSC4 image")
      endif()
      set(QSPI_SRC "${CMAKE_CURRENT_LIST_DIR}/tools/zen-r4-qspi/src")
      set(LAPWING_SRC "${CMAKE_CURRENT_LIST_DIR}/src/zen_r4")
      file(SIZE "${ZMK_JAVELIN_LAPWING_IMAGE}" LAPWING_BYTES)
      execute_process(
        COMMAND ${PYTHON_EXECUTABLE} -c
          "import pathlib,sys,zlib; print(hex(zlib.crc32(pathlib.Path(sys.argv[1]).read_bytes())))"
          "${ZMK_JAVELIN_LAPWING_IMAGE}"
        OUTPUT_VARIABLE LAPWING_CRC OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${ZMK_JAVELIN_LAPWING_IMAGE}" "${QSPI_SRC}/prepare_xip_reader.py")
      set(PATCHED_COMPACT_MAP "${CMAKE_CURRENT_BINARY_DIR}/lapwing/compact_map_dictionary.cc")
      configure_file("${JAVELIN_COMPACT_MAP_SRC}" "${PATCHED_COMPACT_MAP}" COPYONLY)
      execute_process(
        COMMAND ${PYTHON_EXECUTABLE} "${QSPI_SRC}/prepare_xip_reader.py"
          "${PATCHED_COMPACT_MAP}" --copy-lookup-text
        COMMAND_ERROR_IS_FATAL ANY)
      set(JAVELIN_COMPACT_MAP_SRC "${PATCHED_COMPACT_MAP}")
      target_sources(app PRIVATE "${LAPWING_SRC}/lapwing_flash.c" "${QSPI_SRC}/quad.c")
      if(CONFIG_ZMK_JAVELIN_DIAG)
        # Build id shown by the S command: the CI commit, else git describe.
        set(JAVELIN_BUILD_ID "unknown")
        if(DEFINED ENV{GITHUB_SHA})
          string(SUBSTRING "$ENV{GITHUB_SHA}" 0 7 JAVELIN_BUILD_ID)
        else()
          execute_process(
            COMMAND git -C "${CMAKE_CURRENT_LIST_DIR}" describe --always --dirty
            OUTPUT_VARIABLE JAVELIN_GIT_ID OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET RESULT_VARIABLE JAVELIN_GIT_RC)
          if(JAVELIN_GIT_RC EQUAL 0 AND JAVELIN_GIT_ID)
            set(JAVELIN_BUILD_ID "${JAVELIN_GIT_ID}")
          endif()
        endif()
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/diag_build_id.h"
          "#define JAVELIN_BUILD_ID \"${JAVELIN_BUILD_ID}\"\n")
        target_include_directories(app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
        target_sources(app PRIVATE "${LAPWING_SRC}/diag.c")
      endif()
      target_sources_ifdef(CONFIG_ZMK_JAVELIN_BATTERY_LOG app PRIVATE "${LAPWING_SRC}/battery_log.c")
      target_include_directories(app PRIVATE "${QSPI_SRC}")
      target_compile_definitions(app PRIVATE LAPWING_BYTES=${LAPWING_BYTES} LAPWING_CRC=${LAPWING_CRC})
      # Optional second base, provisioned separately without rewriting Lapwing.
      set(PLOVER_BYTES 0)
      set(PLOVER_CRC 0)
      if(ZMK_JAVELIN_PLOVER_IMAGE)
        file(SIZE "${ZMK_JAVELIN_PLOVER_IMAGE}" PLOVER_BYTES)
        execute_process(
          COMMAND ${PYTHON_EXECUTABLE} -c
            "import pathlib,sys,zlib; print(hex(zlib.crc32(pathlib.Path(sys.argv[1]).read_bytes())))"
            "${ZMK_JAVELIN_PLOVER_IMAGE}"
          OUTPUT_VARIABLE PLOVER_CRC OUTPUT_STRIP_TRAILING_WHITESPACE
          COMMAND_ERROR_IS_FATAL ANY)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ZMK_JAVELIN_PLOVER_IMAGE}")
      endif()
      target_compile_definitions(app PRIVATE PLOVER_BYTES=${PLOVER_BYTES} PLOVER_CRC=${PLOVER_CRC})
      # Optional image of the smaller Lapwing dictionaries. Once a build names
      # it, the firmware needs it in flash before onboard Javelin starts.
      set(EXTRAS_BYTES 0)
      set(EXTRAS_CRC 0)
      if(ZMK_JAVELIN_EXTRAS_IMAGE)
        file(SIZE "${ZMK_JAVELIN_EXTRAS_IMAGE}" EXTRAS_BYTES)
        execute_process(
          COMMAND ${PYTHON_EXECUTABLE} -c
            "import pathlib,sys,zlib; print(hex(zlib.crc32(pathlib.Path(sys.argv[1]).read_bytes())))"
            "${ZMK_JAVELIN_EXTRAS_IMAGE}"
          OUTPUT_VARIABLE EXTRAS_CRC OUTPUT_STRIP_TRAILING_WHITESPACE
          COMMAND_ERROR_IS_FATAL ANY)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ZMK_JAVELIN_EXTRAS_IMAGE}")
      endif()
      target_compile_definitions(app PRIVATE EXTRAS_BYTES=${EXTRAS_BYTES} EXTRAS_CRC=${EXTRAS_CRC})
    endif()

    # Core engine sources
    set(JAVELIN_CORE_SRC
      ${JAVELIN_SRC_DIR}/asset_manager.cc
      ${JAVELIN_SRC_DIR}/base64.cc
      ${JAVELIN_SRC_DIR}/bit_field.cc
      ${JAVELIN_SRC_DIR}/bit.cc
      ${JAVELIN_SRC_DIR}/button_script_manager.cc
      ${JAVELIN_SRC_DIR}/button_script.cc
      ${JAVELIN_SRC_DIR}/console_input_buffer.cc
      ${JAVELIN_SRC_DIR}/console.cc
      ${JAVELIN_SRC_DIR}/container/cyclic_queue.cc
      ${JAVELIN_SRC_DIR}/container/list.cc
      ${JAVELIN_SRC_DIR}/crc32.cc
      ${JAVELIN_SRC_DIR}/date_time.cc
      ${JAVELIN_SRC_DIR}/engine_add_translation_mode.cc
      ${JAVELIN_SRC_DIR}/engine_binding.cc
      ${JAVELIN_SRC_DIR}/engine_console_mode.cc
      ${JAVELIN_NORMAL_MODE_SRC}
      ${JAVELIN_SRC_DIR}/engine.cc
      ${JAVELIN_SRC_DIR}/flash.cc
      ${JAVELIN_SRC_DIR}/hash.cc
      ${JAVELIN_SRC_DIR}/host_layout.cc
      ${JAVELIN_SRC_DIR}/key_code.cc
      ${JAVELIN_SRC_DIR}/key_press_parser.cc
      ${JAVELIN_SRC_DIR}/mem.cc
      ${JAVELIN_SRC_DIR}/orthography.cc
      ${JAVELIN_SRC_DIR}/pattern_component.cc
      ${JAVELIN_SRC_DIR}/pattern.cc
      ${JAVELIN_SRC_DIR}/pool_allocate.cc
      ${JAVELIN_SRC_DIR}/random.cc
      ${JAVELIN_SRC_DIR}/script_byte_code.cc
      ${JAVELIN_SRC_DIR}/script_storage.cc
      ${JAVELIN_SRC_DIR}/script.cc
      ${JAVELIN_SEGMENT_BUILDER_SRC}
      ${JAVELIN_SRC_DIR}/segment.cc
      ${JAVELIN_SRC_DIR}/start_console_command_detector.cc
      ${JAVELIN_SRC_DIR}/state.cc
      ${JAVELIN_SRC_DIR}/steno_key_code_buffer_functions.cc
      ${JAVELIN_SRC_DIR}/steno_key_code_buffer.cc
      ${JAVELIN_SRC_DIR}/steno_key_code_emitter.cc
      ${JAVELIN_SRC_DIR}/steno_key_code.cc
      ${JAVELIN_SRC_DIR}/steno_key_state.cc
      ${JAVELIN_SRC_DIR}/str.cc
      ${JAVELIN_SRC_DIR}/stroke_history.cc
      ${JAVELIN_SRC_DIR}/stroke_list_parser.cc
      ${JAVELIN_SRC_DIR}/stroke.cc
      ${JAVELIN_SRC_DIR}/system.cc
      ${JAVELIN_SRC_DIR}/timer_manager.cc
      ${JAVELIN_SRC_DIR}/unicode_script.cc
      ${JAVELIN_SRC_DIR}/unicode.cc
      ${JAVELIN_SRC_DIR}/utf8_pointer.cc
      ${JAVELIN_SRC_DIR}/word_list.cc
      ${JAVELIN_SRC_DIR}/wpm_tracker.cc
      ${JAVELIN_SRC_DIR}/writer.cc
    )

    # Dictionary sources
    set(JAVELIN_DICT_SRC
      ${JAVELIN_SRC_DIR}/dictionary/cache_dictionary.cc
      ${JAVELIN_COMPACT_MAP_SRC}
      ${JAVELIN_SRC_DIR}/dictionary/debug_dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/dictionary_definition.cc
      ${JAVELIN_SRC_DIR}/dictionary/dictionary_list.cc
      ${JAVELIN_SRC_DIR}/dictionary/dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/emily_symbols_dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/full_map_dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/invalid_dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/jeff_numbers_dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/jeff_phrasing_dictionary_data.cc
      ${JAVELIN_SRC_DIR}/dictionary/jeff_phrasing_dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/jeff_show_stroke_dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/orthospelling_data.cc
      ${JAVELIN_SRC_DIR}/dictionary/orthospelling_dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/reverse_auto_suffix_dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/reverse_map_dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/reverse_prefix_dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/reverse_suffix_dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/unicode_dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/user_dictionary.cc
      ${JAVELIN_SRC_DIR}/dictionary/wrapped_dictionary.cc
    )

    # Processor sources
    set(JAVELIN_PROC_SRC
      ${JAVELIN_SRC_DIR}/processor/all_up.cc
      # javelin/processor/fake_processor.cc  # test-only, uses RUN_TESTS APIs
      ${JAVELIN_SRC_DIR}/processor/first_up.cc
      ${JAVELIN_SRC_DIR}/processor/gemini.cc
      ${JAVELIN_SRC_DIR}/processor/jeff_modifiers.cc
      ${JAVELIN_SRC_DIR}/processor/paper_tape.cc
      ${JAVELIN_SRC_DIR}/processor/passport.cc
      ${JAVELIN_SRC_DIR}/processor/plover_hid.cc
      ${JAVELIN_SRC_DIR}/processor/procat.cc
      ${JAVELIN_SRC_DIR}/processor/processor_list.cc
      ${JAVELIN_SRC_DIR}/processor/processor.cc
      ${JAVELIN_SRC_DIR}/processor/repeat.cc
      ${JAVELIN_SRC_DIR}/processor/tx_bolt.cc
    )

    # HAL sources (weak defaults — our shims override the critical ones)
    set(JAVELIN_HAL_SRC
      ${JAVELIN_SRC_DIR}/hal/ble.cc
      ${JAVELIN_SRC_DIR}/hal/bootloader.cc
      ${JAVELIN_SRC_DIR}/hal/connection.cc
      ${JAVELIN_SRC_DIR}/hal/display.cc
      ${JAVELIN_SRC_DIR}/hal/gpio.cc
      ${JAVELIN_SRC_DIR}/hal/infrared.cc
      ${JAVELIN_SRC_DIR}/hal/midi.cc
      ${JAVELIN_SRC_DIR}/hal/mouse.cc
      ${JAVELIN_SRC_DIR}/hal/power.cc
      ${JAVELIN_SRC_DIR}/hal/rgb.cc
      ${JAVELIN_SRC_DIR}/hal/rtc.cc
      ${JAVELIN_SRC_DIR}/hal/serial_port.cc
      ${JAVELIN_SRC_DIR}/hal/sound.cc
      ${JAVELIN_SRC_DIR}/hal/usb_status.cc
    )

    # Font sources (needed by display/script subsystem)
    set(JAVELIN_FONT_SRC
      ${JAVELIN_SRC_DIR}/font/monochrome/data/default.cc
      ${JAVELIN_SRC_DIR}/font/monochrome/data/dos.cc
      ${JAVELIN_SRC_DIR}/font/monochrome/data/huge_digits.cc
      ${JAVELIN_SRC_DIR}/font/monochrome/data/large_digits.cc
      ${JAVELIN_SRC_DIR}/font/monochrome/data/large.cc
      ${JAVELIN_SRC_DIR}/font/monochrome/data/medium_digits.cc
      ${JAVELIN_SRC_DIR}/font/monochrome/data/small_digits.cc
      ${JAVELIN_SRC_DIR}/font/monochrome/font.cc
    )

    # Split sources (ZMK handles split transport, but Javelin split types
    # are referenced by other code)
    set(JAVELIN_SPLIT_SRC
      ${JAVELIN_SRC_DIR}/split/split_console.cc
      ${JAVELIN_SRC_DIR}/split/split_key_state.cc
      ${JAVELIN_SRC_DIR}/split/split_midi.cc
      ${JAVELIN_SRC_DIR}/split/split_power_override.cc
      ${JAVELIN_SRC_DIR}/split/split_serial_buffer.cc
      ${JAVELIN_SRC_DIR}/split/split_usb_status.cc
      ${JAVELIN_SRC_DIR}/split/split_version.cc
      ${JAVELIN_SRC_DIR}/split/split.cc
    )

    # Platform shims (our ZMK implementations)
    set(ZMK_SHIM_SRC
      src/platform/zmk_key_shim.cc
      src/platform/zmk_clock_shim.cc
      src/platform/zmk_flash_shim.cc
      src/platform/zmk_connection_shim.cc
      src/platform/zmk_console_shim.cc
      src/platform/zmk_stubs.cc
    )

    # ZMK behavior driver (C, not C++)
    set(ZMK_BEHAVIOR_SRC
      src/behaviors/behavior_javelin_steno.c
    )

    # Engine initialization
    set(ZMK_INIT_SRC
      src/engine_init.cc
    )

    # Pure Javelin sources (no ZMK deps) → separate library for C++23
    set(JAVELIN_LIB_SRC
      ${JAVELIN_CORE_SRC}
      ${JAVELIN_DICT_SRC}
      ${JAVELIN_PROC_SRC}
      ${JAVELIN_HAL_SRC}
      ${JAVELIN_FONT_SRC}
      ${JAVELIN_SPLIT_SRC}
    )

    # Shims + init (need ZMK headers) → compiled into app target
    set(ZMK_BRIDGE_SRC
      ${ZMK_SHIM_SRC}
      ${ZMK_INIT_SRC}
    )

    # Build everything as a Zephyr library for C++23.
    zephyr_library_named(javelin_steno)
    zephyr_library_sources(${JAVELIN_LIB_SRC} ${ZMK_BRIDGE_SRC})
    if(CONFIG_ZMK_JAVELIN_STENO_MACOS_US_LAYOUT)
      zephyr_library_sources(src/platform/macos_us_layout.cc)
      if(CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY)
        zephyr_library_sources(src/platform/windows_us_layout.cc)
      endif()
    endif()
    # ZMK app headers aren't inherited by libraries — add manually
    zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
    zephyr_library_compile_options(
      -std=gnu++23
      -fno-exceptions
      -fno-rtti
      -fno-threadsafe-statics
      -Wno-invalid-constexpr
      -fpermissive
    )
    zephyr_library_compile_definitions(
      JAVELIN_USE_EMBEDDED_STENO=1
      JAVELIN_USE_USER_DICTIONARY=1
      JAVELIN_PLATFORM_ZEPHYR=1
      JAVELIN_BOARD_CONFIG=<stddef.h>
      M_PI=3.14159265358979323846
      USE_ORTHOGRAPHY_CACHE=0
    )
    # Test suppression handled by include/javelin_overrides/unit_test.h
    # which shadows Javelin's unit_test.h via include path ordering.


    if(CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING)
      zephyr_library_sources("${LAPWING_SRC}/lapwing_dictionary.cc")
      zephyr_library_include_directories(
        "${JAVELIN_SRC_DIR}/dictionary" "${QSPI_SRC}" "${LAPWING_SRC}")
      zephyr_library_compile_definitions(LAPWING_BYTES=${LAPWING_BYTES} PLOVER_BYTES=${PLOVER_BYTES}
        EXTRAS_BYTES=${EXTRAS_BYTES})
      if(CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY)
        zephyr_library_sources("${LAPWING_SRC}/user_dictionary_store.cc" "${LAPWING_SRC}/javelin_web_hid.cc")
        if(CONFIG_ZMK_JAVELIN_STENO_BLE_HID)
          target_sources(app PRIVATE "${LAPWING_SRC}/javelin_ble_hid.c")
        endif()
      endif()
    else()
      # The test fixture uses link-time relocations for native Javelin pointers.
      target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/dicts/cat-test.S)
    endif()

  endif() # central role check

endif() # CONFIG_ZMK_JAVELIN_STENO
