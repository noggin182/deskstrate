#!/usr/bin/env python3
"""Writes the manifest.txt that boards read before pulling an update (see
components/ota_pull/include/ota_pull.h), renaming each image to <project name>.bin.

Version and project name are read from each image's embedded esp_app_desc_t, so the
manifest always agrees with what the board will see after installing it.

    release_manifest.py <tag> <out dir> <firmware.bin>...
"""
import shutil
import struct
import sys
from pathlib import Path

APP_DESC_OFFSET = 0x20  # 24-byte image header + 8-byte first segment header
APP_DESC_MAGIC = 0xABCD5432


def app_desc(image: bytes) -> tuple[str, str]:
    """(project_name, version) from an ESP-IDF app image."""
    magic, = struct.unpack_from("<I", image, APP_DESC_OFFSET)
    if image[0] != 0xE9 or magic != APP_DESC_MAGIC:
        raise ValueError("not an ESP-IDF app image")
    version = image[APP_DESC_OFFSET + 16:APP_DESC_OFFSET + 48].split(b"\0")[0].decode()
    project = image[APP_DESC_OFFSET + 48:APP_DESC_OFFSET + 80].split(b"\0")[0].decode()
    return project, version


def main() -> None:
    tag, out_dir, images = sys.argv[1], Path(sys.argv[2]), sys.argv[3:]
    out_dir.mkdir(parents=True, exist_ok=True)
    lines = [f"tag={tag}"]
    for path in images:
        project, version = app_desc(Path(path).read_bytes())
        shutil.copyfile(path, out_dir / f"{project}.bin")
        lines.append(f"{project}={version}")
        print(f"{project}: {version}")
    (out_dir / "manifest.txt").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
