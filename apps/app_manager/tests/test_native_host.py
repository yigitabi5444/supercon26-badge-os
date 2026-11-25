"""Compile only the actual native host and focused threaded platform doubles."""
import os
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="badge-native-host-") as directory:
    binary = Path(directory) / "native-host-tests"
    command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-pedantic", "-g", "-O1", "-fsanitize=address,undefined", "-pthread",
               "-fno-omit-frame-pointer", "-I", str(app / "tests/native_host_stubs"),
               "-I", str(app), "-I", str(app.parents[1] / "components/os/include"),
               str(app / "native_host.c"), str(app / "native_codex.c"), str(app / "native_hackaday.c"), str(app / "native_messages.c"), str(app / "native_doom.c"), str(app / "tests/native_host_double.c"),
               str(app / "tests/test_native_host.c"), "-lm", "-o", str(binary)]
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True,
                   env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
                        "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
