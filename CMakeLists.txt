if(CONFIG_ZMK_JAVELIN_STENO)

  zephyr_include_directories(include)
  target_sources(app PRIVATE src/behaviors/behavior_javelin_steno.c)

  # Only compile on central side of split keyboards (or non-split)
  if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

    # Patch javelin source for Zephyr compatibility
    set(JAVELIN_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/javelin")
    if(EXISTS "${JAVELIN_SRC_DIR}/dictionary/user_dictionary.h")
      # Remove constexpr from StenoUserDictionaryData constructor
      # (reinterpret_cast not allowed in constexpr on GCC)
      execute_process(
        COMMAND sed -i "s/constexpr StenoUserDictionaryData(const uint8_t/StenoUserDictionaryData(const uint8_t/"
          "${JAVELIN_SRC_DIR}/dictionary/user_dictionary.h"
      )
      # Disable orthography cache (saves ~32KB RAM, avoids missing LockCache/UnlockCache)
      execute_process(
        COMMAND sed -i "s/#define USE_ORTHOGRAPHY_CACHE 1/#define USE_ORTHOGRAPHY_CACHE 0/"
          "${JAVELIN_SRC_DIR}/orthography.h"
      )
      # Strip all test bodies from Javelin .cc files.
      # TEST_BEGIN...TEST_END blocks contain code referencing
      # RUN_TESTS-only APIs. Remove them entirely.
      execute_process(
        COMMAND python3 -c "
import glob, re, os
src = '${JAVELIN_SRC_DIR}'
pattern = re.compile(r'(?:^|\\n)(TEST_BEGIN\\(.*?TEST_END)', re.DOTALL)
count = 0
for f in glob.glob(os.path.join(src, '**/*.cc'), recursive=True):
    t = open(f).read()
    t2 = pattern.sub('', t)
    if t2 != t:
        open(f, 'w').write(t2)
        count += 1
print(f'Stripped test blocks from {count} files')
"
      )
      message(STATUS "Javelin steno: source patches applied")
    endif()

    # Override dir MUST come before javelin/ so our unit_test.h is found first
    zephyr_include_directories(include/javelin_overrides)
    zephyr_include_directories(javelin)
    zephyr_include_directories(include)

    # Core engine sources
    set(JAVELIN_CORE_SRC
      javelin/asset_manager.cc
      javelin/base64.cc
      javelin/bit_field.cc
      javelin/bit.cc
      javelin/button_script_manager.cc
      javelin/button_script.cc
      javelin/console_input_buffer.cc
      javelin/console.cc
      javelin/container/cyclic_queue.cc
      javelin/container/list.cc
      javelin/crc32.cc
      javelin/date_time.cc
      javelin/engine_add_translation_mode.cc
      javelin/engine_binding.cc
      javelin/engine_console_mode.cc
      javelin/engine_normal_mode.cc
      javelin/engine.cc
      javelin/flash.cc
      javelin/hash.cc
      javelin/host_layout.cc
      javelin/key_code.cc
      javelin/key_press_parser.cc
      javelin/mem.cc
      javelin/orthography.cc
      javelin/pattern_component.cc
      javelin/pattern.cc
      javelin/pool_allocate.cc
      javelin/random.cc
      javelin/script_byte_code.cc
      javelin/script_storage.cc
      javelin/script.cc
      javelin/segment_builder.cc
      javelin/segment.cc
      javelin/start_console_command_detector.cc
      javelin/state.cc
      javelin/steno_key_code_buffer_functions.cc
      javelin/steno_key_code_buffer.cc
      javelin/steno_key_code_emitter.cc
      javelin/steno_key_code.cc
      javelin/steno_key_state.cc
      javelin/str.cc
      javelin/stroke_history.cc
      javelin/stroke_list_parser.cc
      javelin/stroke.cc
      javelin/system.cc
      javelin/timer_manager.cc
      javelin/unicode_script.cc
      javelin/unicode.cc
      javelin/utf8_pointer.cc
      javelin/word_list.cc
      javelin/wpm_tracker.cc
      javelin/writer.cc
    )

    # Dictionary sources
    set(JAVELIN_DICT_SRC
      javelin/dictionary/cache_dictionary.cc
      javelin/dictionary/compact_map_dictionary.cc
      javelin/dictionary/debug_dictionary.cc
      javelin/dictionary/dictionary_definition.cc
      javelin/dictionary/dictionary_list.cc
      javelin/dictionary/dictionary.cc
      javelin/dictionary/emily_symbols_dictionary.cc
      javelin/dictionary/full_map_dictionary.cc
      javelin/dictionary/invalid_dictionary.cc
      javelin/dictionary/jeff_numbers_dictionary.cc
      javelin/dictionary/jeff_phrasing_dictionary_data.cc
      javelin/dictionary/jeff_phrasing_dictionary.cc
      javelin/dictionary/jeff_show_stroke_dictionary.cc
      javelin/dictionary/orthospelling_data.cc
      javelin/dictionary/orthospelling_dictionary.cc
      javelin/dictionary/reverse_auto_suffix_dictionary.cc
      javelin/dictionary/reverse_map_dictionary.cc
      javelin/dictionary/reverse_prefix_dictionary.cc
      javelin/dictionary/reverse_suffix_dictionary.cc
      javelin/dictionary/unicode_dictionary.cc
      javelin/dictionary/user_dictionary.cc
      javelin/dictionary/wrapped_dictionary.cc
    )

    # Processor sources
    set(JAVELIN_PROC_SRC
      javelin/processor/all_up.cc
      # javelin/processor/fake_processor.cc  # test-only, uses RUN_TESTS APIs
      javelin/processor/first_up.cc
      javelin/processor/gemini.cc
      javelin/processor/jeff_modifiers.cc
      javelin/processor/paper_tape.cc
      javelin/processor/passport.cc
      javelin/processor/plover_hid.cc
      javelin/processor/procat.cc
      javelin/processor/processor_list.cc
      javelin/processor/processor.cc
      javelin/processor/repeat.cc
      javelin/processor/tx_bolt.cc
    )

    # HAL sources (weak defaults — our shims override the critical ones)
    set(JAVELIN_HAL_SRC
      javelin/hal/ble.cc
      javelin/hal/bootloader.cc
      javelin/hal/connection.cc
      javelin/hal/display.cc
      javelin/hal/gpio.cc
      javelin/hal/infrared.cc
      javelin/hal/midi.cc
      javelin/hal/mouse.cc
      javelin/hal/power.cc
      javelin/hal/rgb.cc
      javelin/hal/rtc.cc
      javelin/hal/serial_port.cc
      javelin/hal/sound.cc
      javelin/hal/usb_status.cc
    )

    # Font sources (needed by display/script subsystem)
    set(JAVELIN_FONT_SRC
      javelin/font/monochrome/data/default.cc
      javelin/font/monochrome/data/dos.cc
      javelin/font/monochrome/data/huge_digits.cc
      javelin/font/monochrome/data/large_digits.cc
      javelin/font/monochrome/data/large.cc
      javelin/font/monochrome/data/medium_digits.cc
      javelin/font/monochrome/data/small_digits.cc
      javelin/font/monochrome/font.cc
    )

    # Split sources (ZMK handles split transport, but Javelin split types
    # are referenced by other code)
    set(JAVELIN_SPLIT_SRC
      javelin/split/split_console.cc
      javelin/split/split_key_state.cc
      javelin/split/split_midi.cc
      javelin/split/split_power_override.cc
      javelin/split/split_serial_buffer.cc
      javelin/split/split_usb_status.cc
      javelin/split/split_version.cc
      javelin/split/split.cc
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


    # The test fixture uses link-time relocations for native Javelin pointers.
    target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/dicts/cat-test.S)

  endif() # central role check

endif() # CONFIG_ZMK_JAVELIN_STENO
