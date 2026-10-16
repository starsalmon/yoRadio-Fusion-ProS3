Import("env")

import glob
import os


def _find_sd_diskio() -> str | None:
    # PlatformIO framework package (arduino-esp32 3.x)
    for base in (
        os.path.join(env.PioPlatform().get_package_dir("framework-arduinoespressif32"), "libraries", "SD", "src"),
        os.path.join(env["PROJECT_PACKAGES_DIR"], "framework-arduinoespressif32", "libraries", "SD", "src"),
    ):
        path = os.path.join(base, "sd_diskio.cpp")
        if os.path.isfile(path):
            return path
    hits = glob.glob(
        os.path.join(env["PROJECT_PACKAGES_DIR"], "**", "sd_diskio.cpp"),
        recursive=True,
    )
    return hits[0] if hits else None


def _patch_sd_diskio(path: str) -> None:
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()

    marker = "/* yoradio SD_SHARE_TFT_SPI */"
    if marker in src:
        print(f"[sd-shared-spi] ok (already patched): {path}")
        return

    hook_decl = """
/* yoradio SD_SHARE_TFT_SPI */
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
extern void yoradio_sd_spi_acquire();
extern void yoradio_sd_spi_release();
#endif
"""

    if hook_decl.strip() not in src:
        needle = '#include "esp_vfs_fat.h"'
        if needle not in src:
            raise RuntimeError("sd_diskio.cpp: cannot find include anchor")
        src = src.replace(needle, needle + hook_decl, 1)

    old_ctor = """  explicit AcquireSPI(ardu_sdcard_t *card) : card(card) {
    card->spi->beginTransaction(SPISettings(card->frequency, MSBFIRST, SPI_MODE0));
  }"""
    new_ctor = """  explicit AcquireSPI(ardu_sdcard_t *card) : card(card) {
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
    yoradio_sd_spi_acquire();
#endif
    card->spi->beginTransaction(SPISettings(card->frequency, MSBFIRST, SPI_MODE0));
  }"""

    old_alt = """  AcquireSPI(ardu_sdcard_t *card, int frequency) : card(card) {
    card->spi->beginTransaction(SPISettings(frequency, MSBFIRST, SPI_MODE0));
  }"""
    new_alt = """  AcquireSPI(ardu_sdcard_t *card, int frequency) : card(card) {
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
    yoradio_sd_spi_acquire();
#endif
    card->spi->beginTransaction(SPISettings(frequency, MSBFIRST, SPI_MODE0));
  }"""

    old_dtor = """  ~AcquireSPI() {
    card->spi->endTransaction();
  }"""
    new_dtor = """  ~AcquireSPI() {
    card->spi->endTransaction();
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
    yoradio_sd_spi_release();
#endif
  }"""

    for old, new in ((old_ctor, new_ctor), (old_alt, new_alt), (old_dtor, new_dtor)):
        if old not in src:
            raise RuntimeError(f"sd_diskio.cpp: pattern missing for patch ({old[:40]}...)")
        src = src.replace(old, new, 1)

    with open(path, "w", encoding="utf-8") as f:
        f.write(src)
    print(f"[sd-shared-spi] patched: {path}")


def _apply_patch() -> None:
    cppflags = env.get("CPPDEFINES", [])
    flat = []
    for item in cppflags:
        if isinstance(item, (list, tuple)):
            flat.append(item[0])
        else:
            flat.append(item)
    share = env.GetProjectOption("custom_sd_share", None)
    enabled = ("SD_SHARE_TFT_SPI" in flat) or (share == "1")
    if not enabled:
        # Also check myoptions via build flag from platformio build_flags
        for flag in env.get("BUILD_FLAGS", []):
            if "SD_SHARE_TFT_SPI=1" in str(flag):
                enabled = True
                break
    if not enabled:
        print("[sd-shared-spi] skip (SD_SHARE_TFT_SPI not enabled)")
        return

    path = _find_sd_diskio()
    if not path:
        print("[sd-shared-spi] warning: sd_diskio.cpp not found")
        return
    _patch_sd_diskio(path)


_apply_patch()
