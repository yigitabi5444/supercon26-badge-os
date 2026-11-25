"""Run only the actual native launcher binding with explicit lifecycle doubles."""
import os
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="badge-native-launcher-") as directory:
    binary = Path(directory) / "native-launcher-tests"
    command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-pedantic", "-g", "-O1", "-fsanitize=address,undefined",
               "-fno-omit-frame-pointer", "-I", str(app / "tests/stubs"), "-I", str(app),
               "-I", str(app.parents[1] / "components/os/include"),
               str(app / "app_manager.c"), str(app / "manager_catalogue.c"),
               str(app / "tests/test_native_launcher.c"), "-o", str(binary)]
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True,
                   env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
                        "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
