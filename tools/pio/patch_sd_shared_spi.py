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


# ---------------------------------------------------------------------------------------------
# Patch 1 (marker SHARE_MARKER): take yoRadio's recursive shared-bus lock in AcquireSPI.
# Patch 2 (marker FIX_MARKER): sd_diskio read-path fixes (CMD12 after multi-block read, short
#   command retries). Every patched line is behind #if defined(SD_SHARE_TFT_SPI) /
#   YORADIO_SD_DISKIO_FIX, both set only by this project's platformio.ini, so other projects
#   that share this framework package compile the original upstream code unchanged.
# Rollback: copy sd_diskio.cpp.yoradio-orig (pristine upstream file) back over sd_diskio.cpp,
#   or reinstall the framework package.
# ---------------------------------------------------------------------------------------------
SHARE_MARKER = "/* yoradio SD_SHARE_TFT_SPI */"
FIX_MARKER = "/* yoradio SD_DISKIO_FIX v1 */"

HOOK_DECL = """
/* yoradio SD_SHARE_TFT_SPI */
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
extern void yoradio_sd_spi_acquire();
extern void yoradio_sd_spi_release();
#endif
"""
INCLUDE_ANCHOR = '#include "esp_vfs_fat.h"'

SHARE_PAIRS = (
    ("""  explicit AcquireSPI(ardu_sdcard_t *card) : card(card) {
    card->spi->beginTransaction(SPISettings(card->frequency, MSBFIRST, SPI_MODE0));
  }""",
     """  explicit AcquireSPI(ardu_sdcard_t *card) : card(card) {
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
    yoradio_sd_spi_acquire();
#endif
    card->spi->beginTransaction(SPISettings(card->frequency, MSBFIRST, SPI_MODE0));
  }"""),
    ("""  AcquireSPI(ardu_sdcard_t *card, int frequency) : card(card) {
    card->spi->beginTransaction(SPISettings(frequency, MSBFIRST, SPI_MODE0));
  }""",
     """  AcquireSPI(ardu_sdcard_t *card, int frequency) : card(card) {
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
    yoradio_sd_spi_acquire();
#endif
    card->spi->beginTransaction(SPISettings(frequency, MSBFIRST, SPI_MODE0));
  }"""),
    ("""  ~AcquireSPI() {
    card->spi->endTransaction();
  }""",
     """  ~AcquireSPI() {
    card->spi->endTransaction();
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
    yoradio_sd_spi_release();
#endif
  }"""),
)

# Helper inserted right before sdCommand().
SDCOMMAND_ANCHOR = "char sdCommand(uint8_t pdrv, char cmd, unsigned int arg, unsigned int *resp) {"
FIX_HELPER = """/* yoradio SD_DISKIO_FIX v1 */
#if defined(YORADIO_SD_DISKIO_FIX) && YORADIO_SD_DISKIO_FIX
// End a multi-block read (CMD18) with CMD12. Upstream sent CMD12 through sdCommand(), which takes
// the first byte with bit 7 clear within 9 bytes as R1. Right after CMD12 the card is still
// clocking out the data block that was in flight, so that byte is usually DATA: it was decoded
// as "crc error", CMD12 was re-sent after delay(100) (bus held), the now-stopped card answered
// "illegal command" ("token error [12] 0x4") and sdReadSectors() failed although every block had
// already been received and CRC16-checked -> FatFS read error -> track EOF after a few seconds.
// R1 of CMD12 carries nothing we need. Send CMD12 + stuff byte, skip Ncr (<= 8 bytes, which also
// swallows the tail of the aborted data block and R1), then wait for the card to leave busy
// (DO idle high). Called with CS low; the caller deselects.
static bool yoradio_sdStopMultiRead(uint8_t pdrv) {
  ardu_sdcard_t *card = s_cards[pdrv];
  static const uint8_t cmd12[7] = {0x4C, 0x00, 0x00, 0x00, 0x00, 0x61, 0xFF};  // CMD12, CRC7=0x30
  card->spi->writeBytes(cmd12, sizeof(cmd12));
  for (int i = 0; i < 8; i++) {
    card->spi->transfer(0xFF);
  }
  int idle = 0;
  uint32_t start = millis();
  do {
    if (card->spi->transfer(0xFF) == 0xFF) {
      if (++idle == 2) {
        return true;
      }
    } else {
      idle = 0;
    }
  } while ((millis() - start) < 500);
  log_w("CMD12: card stayed busy");
  return false;
}
#endif

"""

READ_STOP_OLD = """      if (sdCommand(pdrv, STOP_TRANSMISSION, 0, NULL)) {
        log_e("command failed");
        break;
      }
"""
READ_STOP_NEW = """#if defined(YORADIO_SD_DISKIO_FIX) && YORADIO_SD_DISKIO_FIX
      if (!yoradio_sdStopMultiRead(pdrv)) {
        log_e("stop transmission failed");
        break;
      }
#else
      if (sdCommand(pdrv, STOP_TRANSMISSION, 0, NULL)) {
        log_e("command failed");
        break;
      }
#endif
"""

# sdCommand(): the "no token" / "crc error" retries slept 100 ms with CS just toggled and the
# shared bus (TFT + SD) still held by AcquireSPI -> display and audio reads stalled. A command
# CRC error only needs a resend; card busy is already handled by sdSelectCard()'s sdWait(500).
RETRY_OLD = """      sdDeselectCard(pdrv);
      delay(100);
      sdSelectCard(pdrv);
      continue;"""
RETRY_NEW = """      sdDeselectCard(pdrv);
#if defined(YORADIO_SD_DISKIO_FIX) && YORADIO_SD_DISKIO_FIX
      delay(1);
#else
      delay(100);
#endif
      sdSelectCard(pdrv);
      continue;"""


def _pristine(src: str) -> str:
    """Upstream file content: undo patch 1 if present (patch 2 is never present when called)."""
    if SHARE_MARKER in src:
        src = src.replace(HOOK_DECL, "", 1)
        for old, new in SHARE_PAIRS:
            src = src.replace(new, old, 1)
    return src


def _patch_sd_diskio(path: str) -> None:
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()

    if SHARE_MARKER in src and FIX_MARKER in src:
        print(f"[sd-shared-spi] ok (already patched): {path}")
        return

    backup = path + ".yoradio-orig"
    if not os.path.isfile(backup):
        orig = _pristine(src)
        if SHARE_MARKER in orig or FIX_MARKER in orig:
            raise RuntimeError("sd_diskio.cpp: cannot reconstruct pristine file for backup")
        with open(backup, "w", encoding="utf-8") as f:
            f.write(orig)
        print(f"[sd-shared-spi] backup (pristine upstream): {backup}")

    if SHARE_MARKER not in src:
        if INCLUDE_ANCHOR not in src:
            raise RuntimeError("sd_diskio.cpp: cannot find include anchor")
        src = src.replace(INCLUDE_ANCHOR, INCLUDE_ANCHOR + HOOK_DECL, 1)
        for old, new in SHARE_PAIRS:
            if old not in src:
                raise RuntimeError(f"sd_diskio.cpp: pattern missing for patch ({old[:40]}...)")
            src = src.replace(old, new, 1)

    if FIX_MARKER not in src:
        for needle in (SDCOMMAND_ANCHOR, READ_STOP_OLD):
            if src.count(needle) != 1:
                raise RuntimeError(f"sd_diskio.cpp: expected exactly one ({needle[:40]}...)")
        if src.count(RETRY_OLD) != 2:
            raise RuntimeError("sd_diskio.cpp: expected two sdCommand retry blocks")
        src = src.replace(SDCOMMAND_ANCHOR, FIX_HELPER + SDCOMMAND_ANCHOR, 1)
        src = src.replace(READ_STOP_OLD, READ_STOP_NEW, 1)
        src = src.replace(RETRY_OLD, RETRY_NEW)

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
