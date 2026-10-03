"""Image headers (PNG, JPEG) without decoding: size, format, alpha channel."""

from __future__ import annotations

import struct
from dataclasses import dataclass

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


@dataclass(frozen=True)
class ImageInfo:
    format: str  # "png" | "jpeg"
    width: int
    height: int
    alpha: bool


class ImageError(Exception):
    """Not a readable PNG/JPEG header."""


def image_info(data: bytes) -> ImageInfo:
    if data.startswith(PNG_MAGIC):
        if len(data) < 33 or data[12:16] != b"IHDR":
            raise ImageError("broken PNG header")
        width, height, _, color_type = struct.unpack(">IIBB", data[16:26])
        alpha = (
            color_type in (4, 6)
            or b"tRNS" in data[: data.find(b"IDAT") if b"IDAT" in data else None]
        )
        return ImageInfo("png", width, height, alpha)
    if data.startswith(b"\xff\xd8"):
        i = 2
        while i + 9 < len(data):
            if data[i] != 0xFF:
                raise ImageError("broken JPEG marker")
            marker = data[i + 1]
            if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
                i += 2
                continue
            length = struct.unpack(">H", data[i + 2 : i + 4])[0]
            if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
                height, width = struct.unpack(">HH", data[i + 5 : i + 9])
                return ImageInfo("jpeg", width, height, False)
            i += 2 + length
        raise ImageError("JPEG without frame header")
    raise ImageError("unknown image format (PNG or JPEG expected)")


def is_power_of_two(n: int) -> bool:
    return n > 0 and n & (n - 1) == 0
