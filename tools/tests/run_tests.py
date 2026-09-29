"""Build and run the host rules tests.

    python tools/tests/run_tests.py
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE.parent / "chsim"))
from chsim import find_cxx  # noqa: E402


def main():
    exe = HERE / "build" / "test_rules.exe"
    exe.parent.mkdir(exist_ok=True)
    cmd = find_cxx() + ["-std=gnu++17", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter",
                        "-fsanitize=undefined", "-fno-sanitize-recover=undefined",
                        str(HERE / "test_rules.cpp"), str(ROOT / "src" / "game" / "Round.cpp"),
                        "-o", str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("build failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    raise SystemExit(subprocess.run([str(exe)]).returncode)


if __name__ == "__main__":
    main()
