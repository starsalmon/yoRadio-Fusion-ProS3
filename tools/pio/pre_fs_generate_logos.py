Import("env")

import os
import subprocess
import sys

from SCons.Script import COMMAND_LINE_TARGETS


def _should_run() -> bool:
    # uploadfs invokes buildfs internally, but we handle both explicitly.
    return any(t in ("buildfs", "uploadfs") for t in COMMAND_LINE_TARGETS)


if _should_run():
    script = os.path.join(env["PROJECT_DIR"], "tools", "pio", "gen_station_logos_from_images.py")
    cmd = [sys.executable, script]
    print("[pre_fs] generating station logos:", " ".join(cmd))
    subprocess.check_call(cmd)

