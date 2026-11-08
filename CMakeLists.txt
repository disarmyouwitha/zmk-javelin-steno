if(CONFIG_ZMK_JAVELIN_STENO)

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
    execute_process(
      COMMAND ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_LIST_DIR}/tools/build/prepare_engine.py"
              "${JAVELIN_SRC_DIR}"
      COMMAND_ERROR_IS_FATAL ANY
    )

    # Override dir MUST come before javelin/ so our unit_test.h is found first
    zephyr_include_directories(include/javelin_overrides)
    zephyr_include_directories(${JAVELIN_SRC_DIR})
    zephyr_include_directories(include)

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
      target_include_directories(app PRIVATE "${QSPI_SRC}")
      target_compile_definitions(app PRIVATE LAPWING_BYTES=${LAPWING_BYTES} LAPWING_CRC=${LAPWING_CRC})
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
      ${JAVELIN_SRC_DIR}/engine_normal_mode.cc
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
      ${JAVELIN_SRC_DIR}/segment_builder.cc
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
      zephyr_library_compile_definitions(LAPWING_BYTES=${LAPWING_BYTES})
      if(CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY)
        zephyr_library_sources("${LAPWING_SRC}/user_dictionary_store.cc" "${LAPWING_SRC}/javelin_web_serial.cc")
      endif()
    else()
      # The test fixture uses link-time relocations for native Javelin pointers.
      target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/dicts/cat-test.S)
    endif()

  endif() # central role check

endif() # CONFIG_ZMK_JAVELIN_STENO
