"""mdo hexadecimal version codes; build_id remains an installation serial."""
import re

ORDER_MASK = 0x00FFFFFF
VERSION_1_0 = 0x00010000


def parse(text):
    if not isinstance(text, str) or not re.fullmatch(r"(?:[0-9a-fA-F]{8}|[0-9a-fA-F]{2}(?:\.[0-9a-fA-F]{2}){3})", text):
        raise ValueError("version_code must contain four hexadecimal bytes (XXXXXXXX or XX.XX.XX.XX)")
    return int(text.replace(".", ""), 16)


def release_code(release):
    code = parse(release["version_code"])
    name = release["version_name"]
    if not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", name):
        raise ValueError("version_name must contain three decimal components")
    major, minor, patch = map(int, name.split("."))
    if max(major, minor, patch) > 255:
        raise ValueError("version components must fit in a byte (0..255)")
    order = (major << 16) | (minor << 8) | patch
    expected = order | (0x05000000 if order < VERSION_1_0 else 0)
    if code != expected:
        raise ValueError(f"version_code does not match {name}; expected {expected:08X}")
    return f"{code:08X}"
