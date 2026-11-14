"""Stage a disposable ZMK/Javelin module for the QSPI-to-HID bench test."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

probe = Path(__file__).resolve().parents[1]
repo = probe.parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("workspace", type=Path, help="West workspace initialized from config/west.yml")
engine = parser.parse_args().workspace.resolve() / "javelin-steno"
if not (engine / "engine.h").is_file():
    parser.error(f"Javelin engine checkout missing at {engine}")
module = probe / "build" / "bench-module"
module.mkdir(parents=True, exist_ok=True)
for name in ("CMakeLists.txt", "Kconfig"):
    shutil.copy2(repo / name, module / name)
for name in ("dts", "include", "src", "tools/build", "tools/dict_compiler", "dicts", "zephyr"):
    shutil.copytree(repo / name, module / name, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("__pycache__"))
shutil.copytree(engine, module / "javelin", dirs_exist_ok=True, ignore=shutil.ignore_patterns(".git"))
subprocess.run([sys.executable, str(probe / "src/prepare_xip_reader.py"),
                str(module / "javelin/dictionary/compact_map_dictionary.cc")], check=True)

for name in ("quad.c", "quad.h", "scratch.c", "scratch.h", "mapped.c", "mapped.h",
             "javelin_fixture.S", "javelin_lookup.h", "xip_load.h"):
    shutil.copy2(probe / "src" / name, module / "src" / name)
for name in ("bench_runtime.c", "bench_engine.cc"):
    shutil.copy2(probe / "bench" / name, module / "src" / name)

init = module / "src/engine_init.cc"
source = init.read_text()
begin = source.index("  const auto *collection =")
end = source.index("\n  if (entries.IsEmpty())", begin)
source = source[:begin] + """  extern StenoDictionary *bench_open_dictionary();
  StenoDictionary *external_dictionary = bench_open_dictionary();
  if (!external_dictionary) {
    return false;
  }
  List<StenoDictionaryListEntry> entries;
  entries.Add(StenoDictionaryListEntry(external_dictionary, true));
""" + source[end:]
source = source.replace("collection->dictionaryCount", "1")
init.write_text(source)

fixture = module / "dicts/external-cat-test.bin"
subprocess.run([sys.executable, str(repo / "tools/dict_compiler/compile_dict.py"),
                str(repo / "dicts/cat-test.json"), "--base-addr", "0x12fff000",
                "-o", str(fixture)], check=True)

cmake = module / "CMakeLists.txt"
source = cmake.read_text()
internal_fixture = 'target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/dicts/cat-test.S)'
if source.count(internal_fixture) != 1:
    raise SystemExit("The bench expected one internal dictionary fixture in the ZMK module")
source = source.replace(
    internal_fixture,
    '''target_sources(app PRIVATE src/bench_runtime.c src/quad.c src/scratch.c src/mapped.c src/javelin_fixture.S)
    zephyr_library_sources(src/bench_engine.cc)
    zephyr_library_include_directories(src)
    target_compile_definitions(app PRIVATE JAVELIN_PROBE=1 JAVELIN_FIXTURE_PATH="${CMAKE_CURRENT_LIST_DIR}/dicts/external-cat-test.bin")
    zephyr_ld_options(-Wl,--wrap=hid_int_ep_write)''')
cmake.write_text(source)
