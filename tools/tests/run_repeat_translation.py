"""Run repeat-command regressions against the actual pinned Javelin engine.

Requires a native C++20 compiler. Stages a private engine copy and applies the
same preparation and segment-builder patch as the firmware build.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--build-dir", type=Path, default=Path("build/repeat-native"))
    parser.add_argument("--cxx", default="clang++")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    output = args.build_dir.resolve()
    engine = output / "engine"
    engine.mkdir(parents=True, exist_ok=True)
    shutil.copytree(args.engine.resolve(), engine, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns(".git", "__pycache__"))
    subprocess.run([sys.executable, str(repo / "tools/build/prepare_engine.py"), str(engine)],
                   check=True)
    patched = output / "segment_builder.cc"
    subprocess.run([sys.executable, str(repo / "tools/build/prepare_segment_builder.py"),
                    str(engine / "segment_builder.cc"), str(patched)], check=True)
    objects_dir = output / "obj"
    objects_dir.mkdir(exist_ok=True)
    sources = list(engine.rglob("*.cc"))
    sources.remove(engine / "segment_builder.cc")
    sources += [patched, repo / "tools/tests/repeat_translation.cc"]

    def compile_source(source):
        name = "_".join(source.relative_to(engine).parts) if source.is_relative_to(engine) else source.name
        obj = objects_dir / (name + ".o")
        command = [args.cxx, "-std=c++20", "-O0", "-w", "-DRUN_TESTS=1",
                   "-DJAVELIN_USE_EMBEDDED_STENO=1", "-DJAVELIN_BOARD_CONFIG=<stddef.h>",
                   "-I" + str(engine), "-c", str(source), "-o", str(obj)]
        result = subprocess.run(command, text=True, capture_output=True)
        if result.returncode:
            raise RuntimeError(str(source) + "\n" + result.stderr)
        return str(obj)

    with ThreadPoolExecutor(max_workers=8) as pool:
        objects = list(pool.map(compile_source, sources))
    binary = output / "repeat_translation"
    subprocess.run([args.cxx, *objects, "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
