"""Refuse a USB upload/uploadfs unless the target is April's Speaker (Unexpected Maker FeatherS3).

Works from the USB descriptors only (pyserial list_ports); no serial port is opened.

  1. upload_port must be set (platformio_private.ini, gitignored).
  2. upload_port must be the ONLY Espressif (VID 0x303A) USB device plugged in, so the ProS3 radio
     (or any other ESP32-S3 board) can't be flashed by mistake.
  3. Never a known ProS3: product string containing "ProS3", or a MAC in custom_refuse_macs.
  4. The port must be a FeatherS3:
       - TinyUSB descriptors (UM factory firmware / CircuitPython): PID 0x80D0 / 0x80D6 or product
         "FeatherS3", or
       - USB-Serial/JTAG (PID 0x1001: yoRadio itself, ROM download mode): the USB serial number is the
         chip MAC and must equal custom_upload_mac. That PID is the same on every ESP32-S3, so the
         MAC is the only thing that tells the boards apart there.
"""

Import("env")

from serial.tools import list_ports

ESPRESSIF_VID = 0x303A
USB_SERIAL_JTAG_PID = 0x1001
FEATHERS3_PIDS = {0x80D0, 0x80D6}          # PlatformIO um_feathers3 hwid / arduino-esp32 variant
DEFAULT_REFUSE_MACS = "60:55:f9:f5:59:6c"  # "GoL" ProS3


def _opt(name: str, default: str = "") -> str:
    try:
        return str(env.GetProjectOption(name, default) or default).strip()
    except Exception:
        return default


def _norm_mac(s: str) -> str:
    return (s or "").strip().lower().replace("-", ":")


def _desc(p) -> str:
    vid = f"{p.vid:04X}" if p.vid is not None else "----"
    pid = f"{p.pid:04X}" if p.pid is not None else "----"
    return f"{p.device} [{vid}:{pid}] product={p.product!r} serial={p.serial_number!r}"


def _refuse(msg: str):
    raise SystemExit(f"\nREFUSED (aprils-speaker upload check): {msg}\n")


def _verify_upload_target(source, target, env):
    port = env.subst("$UPLOAD_PORT")
    if not port:
        _refuse("no upload_port. Copy platformio_private.ini.example -> platformio_private.ini and set "
                "upload_port (and custom_upload_mac) for the FeatherS3.")

    ports = list(list_ports.comports())
    esp = [p for p in ports if p.vid == ESPRESSIF_VID]
    me = next((p for p in ports if p.device == port), None)
    if me is None:
        _refuse(f"{port} is not a connected USB serial port. Connected: "
                + (", ".join(_desc(p) for p in esp) or "no Espressif devices"))

    others = [p for p in esp if p.device != port]
    if others:
        _refuse("more than one Espressif USB device is plugged in; unplug everything except the "
                "FeatherS3 (ProS3 radio included):\n  " + "\n  ".join(_desc(p) for p in others))

    refuse_macs = {_norm_mac(m) for m in _opt("custom_refuse_macs", DEFAULT_REFUSE_MACS).replace(",", " ").split() if m}
    product = (me.product or "")
    serial = _norm_mac(me.serial_number or "")
    if "pros3" in product.lower() or serial in refuse_macs:
        _refuse(f"{port} is a ProS3 / refused board: {_desc(me)}")
    if me.vid != ESPRESSIF_VID:
        _refuse(f"{port} is not an Espressif USB device: {_desc(me)}")

    if me.pid in FEATHERS3_PIDS or "feathers3" in product.lower():
        print(f"[aprils-upload] OK: FeatherS3 by USB descriptor: {_desc(me)}")
        return

    want = _norm_mac(_opt("custom_upload_mac"))
    if me.pid == USB_SERIAL_JTAG_PID:
        if not want:
            _refuse(f"{port} is a generic ESP32-S3 USB-Serial/JTAG port ({_desc(me)}). Set "
                    "custom_upload_mac in platformio_private.ini to the FeatherS3's MAC (the serial "
                    "number shown by `pio device list` while only the FeatherS3 is plugged in).")
        if serial != want:
            _refuse(f"{port} MAC {serial or '?'} != custom_upload_mac {want}: {_desc(me)}")
        print(f"[aprils-upload] OK: FeatherS3 by MAC {serial}: {_desc(me)}")
        return

    _refuse(f"{port} is not recognised as a FeatherS3: {_desc(me)}")


env.AddPreAction("upload", _verify_upload_target)
env.AddPreAction("uploadfs", _verify_upload_target)

# Fail before a long compile when an upload is requested.
if any(t in COMMAND_LINE_TARGETS for t in ("upload", "uploadfs")):
    _verify_upload_target(None, None, env)
