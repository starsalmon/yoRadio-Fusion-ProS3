"""Patch Adafruit_SPITFT::initSPI() to honor -D TFT_SCLK / TFT_MOSI on ESP32."""

Import("env")

import os

MARKER = "// yoradio: esp32 remapped tft spi pins"

OLD = """      hwspi._spi->begin();
    }
  } else if (connection == TFT_SOFT_SPI) {"""

NEW = f"""      {MARKER}
#if defined(TFT_SCLK) && defined(TFT_MOSI)
      /* Pins already set in main.cpp — do not spi->end() (breaks remapped FSPI/HSPI). */
#else
      hwspi._spi->begin();
#endif
    }}
  }} else if (connection == TFT_SOFT_SPI) {{"""


def _spitft_path() -> str | None:
    libdeps = env.get("PROJECT_LIBDEPS_DIR")
    pioenv = env.get("PIOENV")
    if not libdeps or not pioenv:
        return None
    return os.path.join(libdeps, pioenv, "Adafruit GFX Library", "Adafruit_SPITFT.cpp")


def apply_adafruit_spitft_pin_patch(*_args, **_kwargs):
    path = _spitft_path()
    if not path or not os.path.isfile(path):
        print(f"[gfx-spi-pins] Adafruit_SPITFT.cpp not found yet: {path}")
        return

    with open(path, "r", encoding="utf-8") as f:
        text = f.read()

    if MARKER in text:
        if "hwspi._spi->end();" in text:
            old_block = f"""      {MARKER}
#if defined(TFT_SCLK) && defined(TFT_MOSI)
      hwspi._spi->end();
#if defined(TFT_MISO) && (TFT_MISO >= 0)
      hwspi._spi->begin(TFT_SCLK, TFT_MISO, TFT_MOSI);
#else
      hwspi._spi->begin(TFT_SCLK, -1, TFT_MOSI);
#endif
#else
      hwspi._spi->begin();
#endif"""
            new_block = f"""      {MARKER}
#if defined(TFT_SCLK) && defined(TFT_MOSI)
      /* Pins already set in main.cpp — do not spi->end() (breaks remapped FSPI/HSPI). */
#else
      hwspi._spi->begin();
#endif"""
            if old_block in text:
                text = text.replace(old_block, new_block, 1)
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text)
                print(f"[gfx-spi-pins] upgraded (removed spi->end): {path}")
                return
        print("[gfx-spi-pins] ok (already patched)")
        return

    if OLD not in text:
        print("[gfx-spi-pins] pattern missing — Adafruit GFX version changed?")
        return

    with open(path, "w", encoding="utf-8") as f:
        f.write(text.replace(OLD, NEW, 1))
    print(f"[gfx-spi-pins] patched: {path}")


env.AddPreAction("buildprog", apply_adafruit_spitft_pin_patch)
try:
    apply_adafruit_spitft_pin_patch()
except Exception as exc:
    print(f"[gfx-spi-pins] early patch skipped: {exc}")
