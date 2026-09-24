"""Expose FreeRDP 3's headers, which its cmake targets place one directory too high."""
from pathlib import Path
import shlex

import buildutil_configure as bc

HEADER = "freerdp/freerdp.h"


def header_root() -> Path:
    pkg_config = bc.tool("pkg-config", install="apt install pkg-config")
    flags = bc.run([pkg_config, "--cflags-only-I", "freerdp3"], what="pkg-config --cflags-only-I freerdp3")
    roots = [Path(flag[2:]).resolve() for flag in shlex.split(flags)]
    found = next((root for root in roots if (root / HEADER).is_file()), None)
    if found is None:
        raise SystemExit(f"freerdp-facade: no -I root of pkg-config freerdp3 ({' '.join(map(str, roots)) or 'none'}) "
                         f"holds {HEADER}")
    return found


def main() -> None:
    target = header_root() / "freerdp"
    link = bc.output_dir() / "freerdp"
    if not link.is_symlink() or link.readlink() != target:
        link.unlink(missing_ok=True)
        link.symlink_to(target, target_is_directory=True)
    bc.depends(target / "freerdp.h")


if __name__ == "__main__":
    main()
