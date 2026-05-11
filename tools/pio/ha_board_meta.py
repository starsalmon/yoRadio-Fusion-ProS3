Import("env")

# Inject Home Assistant device metadata (manufacturer/model/hw_version) from the active
# PlatformIO board manifest (e.g. boards/um_pros3.json or platform package).


def _c_string_literal(s: str) -> str:
    """
    Return a compiler-friendly C string literal value for use in -DNAME=<value>.
    Example: Unexpected Maker -> \"Unexpected Maker\"
    """
    s = (s or "").replace("\\", "\\\\").replace('"', '\\"')
    return f'\\"{s}\\"'


def _has_define(name: str) -> bool:
    # CPPDEFINES may contain:
    # - "NAME"
    # - ("NAME", value)
    # - {"NAME": value}
    defs = env.get("CPPDEFINES", [])
    for d in defs:
        if isinstance(d, str) and d == name:
            return True
        if isinstance(d, (list, tuple)) and len(d) >= 1 and d[0] == name:
            return True
        if isinstance(d, dict) and name in d:
            return True
    return False


try:
    board = env.BoardConfig()
    vendor = board.get("vendor", "")
    model = board.get("name", "")
    mcu = board.get("build.mcu", "")
    variant = board.get("build.variant", "")

    hw_version = ""
    if mcu and variant:
        hw_version = f"{mcu} / {variant}"
    elif mcu:
        hw_version = str(mcu)
    elif variant:
        hw_version = str(variant)

    if vendor and not _has_define("HA_DEVICE_MANUFACTURER"):
        env.Append(CPPDEFINES=[("HA_DEVICE_MANUFACTURER", _c_string_literal(vendor))])

    if model and not _has_define("HA_DEVICE_MODEL"):
        env.Append(CPPDEFINES=[("HA_DEVICE_MODEL", _c_string_literal(model))])

    if hw_version and not _has_define("HA_DEVICE_HW_VERSION"):
        env.Append(CPPDEFINES=[("HA_DEVICE_HW_VERSION", _c_string_literal(hw_version))])
except Exception as e:
    print(f"[ha-board-meta] skipped: {e}")

