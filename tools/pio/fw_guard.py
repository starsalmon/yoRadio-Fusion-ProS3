"""Post-build guard: refuse a firmware that lost the yoRadio framework patches.

The shared pioarduino Arduino framework under ~/.platformio is reinstalled whenever a build
switches between projects/envs with a different custom_sdkconfig, which silently restores the
stock liblwip.a / libesp_netif.a and the stock sd_diskio.cpp. tools/pio/apply_audio_idf_mod.py
and tools/pio/patch_sd_shared_spi.py re-apply them; this script proves it worked, per build.

Checks (after firmware.bin is made):
  1. Audio_IDF_MOD lwip is linked: function sizes in firmware.elf match the mod build
       netconn_alloc 0x78, tcp_recved 0x72, poll_tcp 0x69, tcp_rst_common 0x5f
     (stock 3.3.6 lwip: 0x77, 0x6f, 0x65, 0x62 -> ~1.5 s web-radio stalls).
  2. If -DYORADIO_SD_DISKIO_FIX=1: the patched sd_diskio is linked (its CMD12 log strings
     "CMD12: card stayed busy" and "stop transmission failed" are in firmware.bin).
On failure firmware.bin is renamed to firmware.bin.REJECTED and the build fails, so the bad
image can't be flashed by accident. Set YORADIO_FW_GUARD=0 to bypass (not recommended).
"""

Import("env")

import os
import subprocess

LWIP_MOD = {"netconn_alloc": 0x78, "tcp_recved": 0x72, "poll_tcp": 0x69, "tcp_rst_common": 0x5F}
LWIP_STOCK = {"netconn_alloc": 0x77, "tcp_recved": 0x6F, "poll_tcp": 0x65, "tcp_rst_common": 0x62}
CMD12_STRINGS = (b"CMD12: card stayed busy", b"stop transmission failed")


def _flag_on(name: str) -> bool:
    for flag in env.get("BUILD_FLAGS", []):
        s = str(flag)
        if s in (f"-D{name}=1", f"-D{name}"):
            return True
    for item in env.get("CPPDEFINES", []):
        if isinstance(item, (list, tuple)):
            if item[0] == name and str(item[1]) not in ("0", ""):
                return True
        elif item == name:
            return True
    return False


def _nm_sizes(elf: str) -> dict:
    nm = env.subst("$CC").replace("gcc", "nm")
    out = subprocess.run([nm, "-S", elf], capture_output=True, text=True, check=True,
                         env=env["ENV"]).stdout
    sizes = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3] in LWIP_MOD:
            sizes[parts[3]] = int(parts[1], 16)
    return sizes


def _guard(target, source, env):
    if os.environ.get("YORADIO_FW_GUARD", "1") == "0":
        print("[fw-guard] BYPASSED (YORADIO_FW_GUARD=0)")
        return 0
    build_dir = env.subst("$BUILD_DIR")
    progname = env.subst("${PROGNAME}")
    elf = os.path.join(build_dir, progname + ".elf")
    binf = os.path.join(build_dir, progname + ".bin")
    errors = []

    sizes = _nm_sizes(elf)
    if sizes == LWIP_MOD:
        print("[fw-guard] lwip: Audio_IDF_MOD build linked (OK)")
    elif sizes == LWIP_STOCK:
        errors.append("lwip: STOCK liblwip.a linked (Audio_IDF_MOD swap missing)")
    else:
        errors.append(f"lwip: unexpected symbol sizes {sizes}")

    if _flag_on("YORADIO_SD_DISKIO_FIX"):
        with open(binf, "rb") as f:
            blob = f.read()
        missing = [s.decode() for s in CMD12_STRINGS if s not in blob]
        if missing:
            errors.append(f"sd_diskio: CMD12 fix strings missing {missing} (stock sd_diskio linked)")
        else:
            print("[fw-guard] sd_diskio: CMD12 fix linked (OK)")

    if errors:
        rejected = binf + ".REJECTED"
        if os.path.exists(rejected):
            os.remove(rejected)
        os.replace(binf, rejected)
        for e in errors:
            print(f"[fw-guard] FAIL: {e}")
        print(f"[fw-guard] firmware.bin renamed to {rejected}; rebuild after fixing the framework")
        return 1
    return 0


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _guard)
