"""Identify the shader cache and Vulkan device used by diagnostic captures."""
import hashlib
from pathlib import Path
import re

from cooked_scene_smoke import sha256_file


def shader_identity(directory):
    directory = Path(directory)
    paths = sorted(directory.rglob("*.spv"))
    if not paths or any(path.is_symlink() for path in directory.rglob("*")) or any(not path.is_file() for path in paths):
        raise ValueError("shader cache is missing or contains non-file shader entries")
    digest = hashlib.sha256()
    for path in paths:
        relative = path.relative_to(directory).as_posix().encode("utf-8")
        digest.update(len(relative).to_bytes(8, "big"))
        digest.update(relative)
        digest.update(bytes.fromhex(sha256_file(path)))
    return {"sha256": digest.hexdigest(), "files": len(paths)}


def device_identity(log):
    devices = re.findall(r"Created VkDevice to run on GPU (.+?) with the following", log)
    drivers = re.findall(r'Using "[^"]+" with driver: "([^"]+)"', log)
    if len(devices) != 1 or len(drivers) != 1:
        return None
    driver = Path(drivers[0]).resolve(strict=True)
    return {"gpu": devices[0], "driver_path": str(driver), "driver_sha256": sha256_file(driver)}
