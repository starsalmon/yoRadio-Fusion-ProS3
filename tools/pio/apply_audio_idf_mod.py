Import("env")

import glob
import hashlib
import os
import shutil


def _sha1(path: str) -> str:
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _find_first(base_dir: str, filename: str) -> str | None:
    hits = glob.glob(os.path.join(base_dir, "**", filename), recursive=True)
    return hits[0] if hits else None


def _copy_if_different(src: str, dst: str) -> bool:
    if not os.path.isfile(src):
        print(f"[audio-idf-mod] missing source: {src}")
        return False
    if os.path.isfile(dst):
        try:
            if _sha1(src) == _sha1(dst):
                print(f"[audio-idf-mod] ok (already patched): {os.path.basename(dst)}")
                return True
        except Exception:
            # If hashing fails for any reason, fall back to copy.
            pass
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)
    print(f"[audio-idf-mod] patched: {dst}")
    return True


def _locate_audio_idf_mod_dir(project_dir: str) -> str | None:
    # Priority order:
    # 1) Explicit env var
    # 2) Project-local folder (recommended): tools/Audio_IDF_MOD
    # 3) Project-local folder (legacy): Audio_IDF_MOD
    # 4) Your known Downloads location (convenience fallback)
    explicit = os.getenv("AUDIO_IDF_MOD_DIR")
    if explicit and os.path.isdir(explicit):
        return explicit

    local_tools = os.path.join(project_dir, "tools", "Audio_IDF_MOD")
    if os.path.isdir(local_tools):
        return local_tools

    local = os.path.join(project_dir, "Audio_IDF_MOD")
    if os.path.isdir(local):
        return local

    downloads_fallback = os.path.expanduser("~/Downloads/yoRadio-Fusion-main/Audio_IDF_MOD")
    if os.path.isdir(downloads_fallback):
        return downloads_fallback

    return None


def _locate_esp32s3_libdir() -> str | None:
    home = os.path.expanduser("~")
    packages_base = os.path.join(home, ".platformio", "packages")
    if not os.path.isdir(packages_base):
        return None

    # PioArduino still uses the same libs package name.
    pkgs = sorted(glob.glob(os.path.join(packages_base, "framework-arduinoespressif32-libs*")))
    for pkg in pkgs:
        libdir = os.path.join(pkg, "esp32s3", "lib")
        if os.path.isdir(libdir):
            # Sanity check: confirm expected archives exist
            if os.path.isfile(os.path.join(libdir, "liblwip.a")) and os.path.isfile(os.path.join(libdir, "libesp_netif.a")):
                return libdir
    return None


def _project_font_src(project_dir: str) -> str:
    return os.path.join(project_dir, "fonts", "glcdfont_EN.c")


def _libdeps_gfx_glcdfont_dst() -> str | None:
    # Prefer patching the project-local libdeps copy used for builds.
    project_libdeps_dir = env.get("PROJECT_LIBDEPS_DIR")
    if not project_libdeps_dir:
        return None
    pioenv = env.get("PIOENV")
    if not pioenv:
        return None
    return os.path.join(project_libdeps_dir, pioenv, "Adafruit GFX Library", "glcdfont.c")


def apply_gfx_glcdfont_patch(*args, **kwargs):
    project_dir = env["PROJECT_DIR"]
    src = _project_font_src(project_dir)
    if not os.path.isfile(src):
        print(f"[gfx-font] missing source: {src}")
        return

    dst = _libdeps_gfx_glcdfont_dst()
    if not dst:
        print("[gfx-font] could not locate PROJECT_LIBDEPS_DIR/PIOENV for Adafruit GFX")
        return

    if not os.path.isfile(dst):
        print(f"[gfx-font] destination not found yet (will patch when available): {dst}")
        return

    if _copy_if_different(src, dst):
        print(f"[gfx-font] done (using {os.path.relpath(src, project_dir)})")


def apply_audio_idf_mod(*args, **kwargs):
    project_dir = env["PROJECT_DIR"]
    mod_dir = _locate_audio_idf_mod_dir(project_dir)
    if not mod_dir:
        print(
            "[audio-idf-mod] Audio_IDF_MOD not found. Put it in PROJECT_DIR/Audio_IDF_MOD\n"
            "                or set AUDIO_IDF_MOD_DIR=/path/to/Audio_IDF_MOD"
        )
        return

    src_lwip = _find_first(mod_dir, "liblwip.a")
    src_netif = _find_first(mod_dir, "libesp_netif.a")
    if not src_lwip or not src_netif:
        print(f"[audio-idf-mod] invalid Audio_IDF_MOD folder (missing .a files): {mod_dir}")
        return

    dst_libdir = _locate_esp32s3_libdir()
    if not dst_libdir:
        print("[audio-idf-mod] could not locate PlatformIO esp32s3 libs directory under ~/.platformio/packages/")
        return

    ok1 = _copy_if_different(src_lwip, os.path.join(dst_libdir, "liblwip.a"))
    ok2 = _copy_if_different(src_netif, os.path.join(dst_libdir, "libesp_netif.a"))
    if ok1 and ok2:
        print(f"[audio-idf-mod] done (from {mod_dir})")


# Apply patches during the build (also safe to re-run).
env.AddPreAction("buildprog", apply_audio_idf_mod)
env.AddPreAction("buildprog", apply_gfx_glcdfont_patch)

# Also attempt immediately at script load time (helps when libdeps already exist).
try:
    apply_gfx_glcdfont_patch()
except Exception:
    pass

