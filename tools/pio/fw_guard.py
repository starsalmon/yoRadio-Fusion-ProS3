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
# Known-good Audio_IDF_MOD variants: lib md5s + linked marker sizes (see tools/Audio_IDF_MOD/<variant>/README.md)
VARIANTS = {
    "CORE_3.3.6-IDF_ver5.5.2": {
        "liblwip.a": "16e4a2d94615a86b506f2b7999adb693", "libesp_netif.a": "a4c11c7f5c9f3ed6ec9e7a123c2876e0",
        "markers": LWIP_MOD, "tcp_wnd": 0x8000,
    },
    "CORE_3.3.6-IDF_ver5.5.2-wnd64k": {
        "liblwip.a": "65a4e6502d190691e53fe9c99bcc3368", "libesp_netif.a": "a4c11c7f5c9f3ed6ec9e7a123c2876e0",
        "markers": {"netconn_alloc": 0x78, "tcp_recved": 0x6E, "poll_tcp": 0x69, "tcp_rst_common": 0x62}, "tcp_wnd": 0xFFFF,
    },
}
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


def _framework_libdir():
    import glob
    hits = glob.glob(os.path.expanduser("~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/lib"))
    return hits[0] if hits else None


def _md5(path):
    import hashlib
    if not os.path.isfile(path):
        return None
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def _tcp_alloc_wnd(elf):
    """rcv_wnd/rcv_ann_wnd init in tcp_alloc (ELF bytes; the host objdump can't decode xtensa-s3):
    wnd64k: movi.n a8,-1 ; s32i a8,a2,84  -> bytes 7c f8 82 62 15  => 0xFFFF
    32k:    l32r a8,<lit 0x80008000> ; s32i a8,a2,84 (literal 00 80 00 80 in the pool before it) => 0x8000"""
    import struct
    nm = env.subst("$CC").replace("gcc", "nm")
    out = subprocess.run([nm, "-S", elf], capture_output=True, text=True, env=env["ENV"]).stdout
    addr = size = None
    for line in out.splitlines():
        p = line.split()
        if len(p) == 4 and p[3] == "tcp_alloc":
            addr, size = int(p[0], 16), int(p[1], 16)
    if addr is None:
        return None
    with open(elf, "rb") as f:
        d = f.read()
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", d, 0x2E)
    code = None
    for k in range(shnum):
        _, _, _, sh_addr, sh_off, sh_size = struct.unpack_from("<IIIIII", d, shoff + k * shentsize)
        if sh_addr <= addr < sh_addr + sh_size:
            code = d[sh_off + addr - sh_addr: sh_off + addr - sh_addr + size]
            lit = d[max(sh_off, sh_off + addr - sh_addr - 1024): sh_off + addr - sh_addr]
            break
    if code is None:
        return None
    if b"\x7c\xf8\x82\x62\x15" in code:
        return 0xFFFF
    if b"\x82\x62\x15" in code and b"\x00\x80\x00\x80" in lit:
        return 0x8000
    return None


def _guard(target, source, env):
    if os.environ.get("YORADIO_FW_GUARD", "1") == "0":
        print("[fw-guard] BYPASSED (YORADIO_FW_GUARD=0)")
        return 0
    build_dir = env.subst("$BUILD_DIR")
    progname = env.subst("${PROGNAME}")
    elf = os.path.join(build_dir, progname + ".elf")
    binf = os.path.join(build_dir, progname + ".bin")
    errors = []

    try:
        variant = str(env.GetProjectOption("custom_audio_idf_mod_variant", "")).strip() or "CORE_3.3.6-IDF_ver5.5.2"
    except Exception:
        variant = "CORE_3.3.6-IDF_ver5.5.2"
    want = VARIANTS.get(variant)
    sizes = _nm_sizes(elf)
    if want is None:
        errors.append(f"lwip: unknown Audio_IDF_MOD variant {variant!r} (add it to VARIANTS with md5s/markers)")
    else:
        libdir = _framework_libdir()
        for lib in ("liblwip.a", "libesp_netif.a"):
            got = _md5(os.path.join(libdir, lib)) if libdir else None
            if got != want[lib]:
                errors.append(f"lwip: framework {lib} md5 {got} != variant {variant} {want[lib]}")
        if sizes == LWIP_STOCK:
            errors.append("lwip: STOCK liblwip.a linked (Audio_IDF_MOD swap missing)")
        elif want["markers"] is not None and sizes != want["markers"]:
            errors.append(f"lwip: marker sizes {sizes} != variant {variant} {want['markers']}")
        elif want["markers"] is None:
            errors.append(f"lwip: variant {variant} has no recorded markers; measured {sizes}")
        wnd = _tcp_alloc_wnd(elf)
        if wnd != want["tcp_wnd"]:
            errors.append(f"lwip: tcp_alloc receive window {wnd} != {want['tcp_wnd']:#x}")
        if not errors:
            print(f"[fw-guard] lwip: Audio_IDF_MOD variant {variant} linked, tcp window {wnd:#x} (OK)")

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
