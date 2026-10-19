"""Refuse USB upload unless the target is April's Speaker (Waveshare S3-Zero)."""

import glob
import os
import re
import subprocess

Import("env")

APRILS_SPEAKER_MAC = "ac:27:6e:b0:3e:3c"
GOL_MAC = "60:55:f9:f5:59:6c"


def _probe_port(port: str, required=True):
    try:
        result = subprocess.run(
            ["python3", "-m", "esptool", "--port", port, "chip-id"],
            capture_output=True,
            text=True,
            timeout=12,
            check=False,
        )
    except Exception as exc:
        if required:
            raise SystemExit(f"\nREFUSED: Could not probe {port}: {exc}\n")
        return None

    out = (result.stdout or "") + (result.stderr or "")
    if result.returncode != 0:
        if required:
            raise SystemExit(f"\nREFUSED: esptool could not open {port}:\n{out}\n")
        return None
    return out


def _mac_from_probe(out: str):
    match = re.search(r"MAC:\s+([0-9a-f:]{17})", out, re.IGNORECASE)
    return match.group(1).lower() if match else None


def _list_usbmodem_ports() -> list[str]:
    return sorted(glob.glob("/dev/cu.usbmodem*"))


def _refuse_if_gol_still_plugged() -> None:
    for port in _list_usbmodem_ports():
        out = _probe_port(port, required=False)
        if not out:
            continue
        mac = _mac_from_probe(out)
        if mac == GOL_MAC:
            raise SystemExit(
                "\nREFUSED: GoL ProS3 is still on USB (MAC 60:55:f9:f5:59:6c).\n"
                f"  Found on {port}. Unplug GoL, leave only April's Speaker, then retry.\n"
            )


def _verify_upload_target(source, target, env):
    port = env.subst("$UPLOAD_PORT")
    if not port:
        raise SystemExit(
            "\nREFUSED: No upload_port set for aprils-speaker.\n"
            "  Copy platformio_private.ini.example -> platformio_private.ini\n"
            "  and set upload_port to the S3-Zero only (unplug GoL ProS3 first).\n"
        )

    _refuse_if_gol_still_plugged()

    out = _probe_port(port, required=True)
    out_l = out.lower()
    mac = _mac_from_probe(out)

    if mac == GOL_MAC:
        raise SystemExit(
            "\nREFUSED: upload port is GoL ProS3 (MAC 60:55:f9:f5:59:6c), not April's Speaker.\n"
            "  Unplug GoL and set upload_port to the S3-Zero in platformio_private.ini.\n"
        )

    if mac != APRILS_SPEAKER_MAC:
        raise SystemExit(
            f"\nREFUSED: {port} MAC {mac or '?'} is not April's Speaker "
            f"(expected {APRILS_SPEAKER_MAC}).\n"
            "  Unplug GoL, confirm only the S3-Zero is connected, update upload_port.\n"
            f"  esptool output tail:\n{out[-500:]}\n"
        )

    if "embedded flash 4mb" not in out_l:
        raise SystemExit(
            f"\nREFUSED: {port} is not a Waveshare S3-Zero (need Embedded Flash 4MB).\n"
            f"  esptool output tail:\n{out[-500:]}\n"
        )


env.AddPreAction("upload", _verify_upload_target)

# Fail before a long compile when upload is requested.
if "upload" in COMMAND_LINE_TARGETS:
    _verify_upload_target(None, None, env)
