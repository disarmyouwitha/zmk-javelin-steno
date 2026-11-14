#!/usr/bin/env python3
"""Render the actual RHS widgets and stress transitions within its display heap.

Requires CMake, Ninja and a native C compiler. Uses LVGL's native allocator;
the firmware uses Zephyr's heap. Native pointers are larger, so the memory
measurement is a conservative desktop check, not a device heap measurement.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('lvgl', type=Path)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/display-preview')
    parser.add_argument('--heap-size', type=int, help='Override the RHS heap for failure reproduction')
    parser.add_argument('--no-translation', action='store_true')
    args = parser.parse_args()
    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    config = (ROOT / 'config/steno-squid-split/right-nice-view.conf').read_text()
    heap = args.heap_size or int(re.search(r'^CONFIG_LV_Z_MEM_POOL_SIZE=(\d+)$', config, re.M)[1])
    (output / 'lv_conf.h').write_text('''#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 8
#define LV_USE_LOG 0
#define LV_DRAW_SW_SUPPORT_L8 1
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_NULL 1
#define LV_ASSERT_HANDLER __builtin_trap();
''' + f'#define LV_MEM_SIZE {heap}\n' + ''.join(
        f'#define LV_FONT_MONTSERRAT_{size} 1\n' for size in (8, 10, 12, 14, 16, 20, 28)
    ) + '#define LV_FONT_UNSCII_8 1\n#endif\n')
    stubs = output / 'stubs'
    (stubs / 'zephyr/sys').mkdir(parents=True, exist_ok=True)
    (stubs / 'zmk').mkdir(exist_ok=True)
    (stubs / 'zephyr/toolchain.h').write_text(
        '#pragma once\n#define BUILD_ASSERT(x, ...) _Static_assert(x, #x)\n')
    (stubs / 'zephyr/sys/util.h').write_text(
        '#include <zephyr/toolchain.h>\n#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))\n')
    (stubs / 'zmk/activity.h').write_text('''enum zmk_activity_state {
    ZMK_ACTIVITY_ACTIVE, ZMK_ACTIVITY_IDLE
};
static enum zmk_activity_state zmk_activity_get_state(void) { return ZMK_ACTIVITY_ACTIVE; }
''')
    cmake_conf = 'set(LV_CONF_PATH ' + json.dumps(chr(34) + str(output / 'lv_conf.h') + chr(34)) + ')'
    (output / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.20)
project(display_preview C)
{cmake_conf}
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
add_subdirectory("{args.lvgl.resolve()}" lvgl)
add_executable(preview "{ROOT / 'tools/tests/display_preview.c'}")
target_include_directories(preview PRIVATE "${{CMAKE_CURRENT_SOURCE_DIR}}/stubs")
target_link_libraries(preview PRIVATE lvgl m)
{'' if args.no_translation else 'target_compile_definitions(preview PRIVATE CONFIG_ZMK_STENO_SQUID_STROKE_TRANSLATION=1)'}
''')
    with (output / 'build.log').open('w') as log:
        subprocess.run(['cmake', '-S', str(output), '-B', str(output / 'out'), '-G', 'Ninja'],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(['cmake', '--build', str(output / 'out'), '-j', '8'],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run([str(output / 'out/preview')], cwd=output, check=True, timeout=30)
    print(f'Rendered screenshots: {output}')


if __name__ == '__main__':
    main()
