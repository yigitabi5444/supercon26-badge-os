"""Focused native target factory/default profile checks, with no network or device I/O."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

app = Path(__file__).resolve().parents[1]
sources = ["controller.c", "controller_target.c", "target_profile.c", "manager_catalogue.c", "native_elf.c",
           "txn_json.c", "txn_package.c", "txn_store.c", "txn_geometry.c", "txn_littlefs.c",
           "package_source.c", "package_config.c", "package_http.c", "video_source.c", "video_asset.c",
           "tests/test_target_controller.c"]
with tempfile.TemporaryDirectory(prefix="badge-native-target-") as directory:
    for helper, date_check in ([(True, True)] if '--quick' in sys.argv else
                               [(False, False), (False, True), (True, False), (True, True)]):
        binary = Path(directory) / ("native-target-helper-"+str(helper)+"-date-"+str(date_check))
        command = [os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                   "-Wall", "-Wextra", "-Werror", "-pedantic", "-g", "-O1",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   "-Itests/local_https_stubs", "-Itests/fetch_stubs", "-Itests/target_stubs", "-Itests/stubs", "-I../../components/os/include"]
        if date_check:
            command.append("-DCONFIG_MBEDTLS_HAVE_TIME_DATE=1")
        if helper:
            command.append("-DPROVIDE_HOME_END")
        elif sys.platform == "darwin":
            # Permit only this deliberately absent weak companion API, never other unresolved symbols.
            command.append("-Wl,-U,_btn_home_prompt_end")
        if '--fresh-layout' in sys.argv:
            command.append("-DCONFIG_BADGE_APP_MANAGER_FRESH_LAYOUT=1")
        subprocess.run(command + sources + ["-o", str(binary)], cwd=app, check=True)
        subprocess.run([str(binary)], check=True,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
                            "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
    selector=Path(directory)/"namespace-selector"
    subprocess.run(command+["txn_geometry.c","tests/test_littlefs_select.c","-o",str(selector)],
                   cwd=app,check=True)
    subprocess.run([str(selector)],check=True)
