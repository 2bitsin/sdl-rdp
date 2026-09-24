"""Build SDL from its release archive with the rdp driver patched in, configured by SDL's own CMake."""
import hashlib
import json
import shlex
import shutil
import tarfile
import tomllib
import urllib.request
from dataclasses import dataclass
from pathlib import Path

import buildutil_configure as bc

ROOT     = bc.source_dir()
VERSIONS = ROOT / "versions.toml"
PATCH    = ROOT / "rdp-driver.patch"
DISABLED = ("X11", "WAYLAND", "OPENGL", "OPENGLES", "VULKAN", "GPU", "ALSA", "PULSEAUDIO", "SNDIO", "DBUS",
            "LIBUDEV", "HIDAPI", "CAMERA", "TRAY", "DIALOG", "TESTS", "EXAMPLES")
ENABLED  = ("UNIX_CONSOLE_BUILD", "RDP", "RDPAUDIO", "RDPSTORAGE", "SHARED")
# NDEBUG and DEBUG follow the build type of the build that compiles SDL, never SDL's own configure.
BUILD_TYPE_DEFINES = frozenset(("NDEBUG", "DEBUG", "_DEBUG"))
OPTION_PREFIXES    = ("-m", "-W")
OPTION_FLAGS       = frozenset(("-pthread", "-fno-strict-aliasing", "-fno-strict-overflow"))
DRIVER_OPTIONS     = ("-Wsign-compare",)


@dataclass(frozen=True)
class Release:
    version: str
    sha256: str

    @property
    def url(self):
        return (f"https://github.com/libsdl-org/SDL/releases/download/release-{self.version}/"
                f"SDL3-{self.version}.tar.gz")

    @property
    def root(self):
        return bc.output_dir(shared=True) / self.version


def pinned_release():
    # buildutil #136: the hook receives no [options], so the release is pinned here.
    pins    = tomllib.loads(VERSIONS.read_text())
    version = pins["build"]
    if version not in pins["sha256"]:
        raise SystemExit(f"SDL {version} is unlisted; extend {VERSIONS}")
    return Release(version, pins["sha256"][version])


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def download(release):
    archive = release.root / f"SDL3-{release.version}.tar.gz"
    if archive.exists() and digest(archive) == release.sha256:
        return archive
    temporary = archive.with_suffix(".tmp")
    print(f"Downloading {release.url}", flush=True)
    with urllib.request.urlopen(release.url) as source, temporary.open("wb") as target:
        shutil.copyfileobj(source, target)
    if digest(temporary) != release.sha256:
        temporary.unlink()
        raise SystemExit(f"SDL archive {release.url} does not match its pinned sha256")
    temporary.replace(archive)
    return archive


def patched_source(release, fingerprint):
    source = release.root / f"SDL3-{release.version}"
    stamp  = release.root / "extracted.sha256"
    if source.is_dir() and stamp.exists() and stamp.read_text() == fingerprint:
        return source
    shutil.rmtree(source, ignore_errors=True)
    with tarfile.open(download(release)) as package:
        package.extractall(release.root, filter="data")
    bc.run(["patch", "-p1", "--batch", "-i", PATCH], what="applying rdp-driver.patch", cwd=source)
    stamp.write_text(fingerprint)
    return source


def configure(release, source, fingerprint):
    config = release.root / "config"
    stamp  = release.root / "configured.sha256"
    if stamp.exists() and stamp.read_text() == fingerprint:
        return config
    shutil.rmtree(config, ignore_errors=True)
    command  = ["cmake", "-S", source, "-B", config, "-G", "Ninja", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    command += [f"-DSDL_{option}=OFF" for option in DISABLED]
    command += [f"-DSDL_{option}=ON" for option in ENABLED]
    command += ["-DSDL_RDP_DYNAMIC=libsdl-rdp-backend.so", "-DSDL_STATIC=OFF", "-DSDL_TEST_LIBRARY=OFF",
                "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON"]
    bc.run(command, what="SDL's CMake configure")
    stamp.write_text(fingerprint)
    return config


def arguments(entry):
    return entry.get("arguments") or shlex.split(entry["command"])


def c_entries(config):
    entries = json.loads((config / "compile_commands.json").read_text())
    return [entry for entry in entries if Path(entry["file"]).suffix == ".c"]


def defines(entry):
    split = (argument[2:].partition("=") for argument in arguments(entry) if argument.startswith("-D"))
    return [(name, value) for name, _, value in split if name not in BUILD_TYPE_DEFINES]


def compile_options(entry):
    return [argument for argument in arguments(entry)
            if argument.startswith(OPTION_PREFIXES) or argument in OPTION_FLAGS]


def uniform_options(entries):
    options = {tuple(compile_options(entry)) for entry in entries}
    if len(options) != 1:
        raise SystemExit(f"SDL compiles its C sources with {len(options)} option sets; this hook carries one")
    return list(options.pop())


def build_config(config, driver):
    header  = next(config.rglob("SDL_build_config.h")).read_text()
    lines   = ["", "#ifndef SDL_rdp_build_defines_h_", "#define SDL_rdp_build_defines_h_"]
    # buildutil #136: the hook receives no build type, so DEBUG follows NDEBUG.
    lines  += ["#ifndef NDEBUG", "#define DEBUG 1", "#endif"]
    lines  += [f"#define {name} {value or 1}" for name, value in defines(driver)]
    lines  += ["#endif", ""]
    bc.emit("SDL_build_config.h", header + "\n".join(lines))


def option_files(options):
    bc.emit("sdl-c.rsp", "\n".join(options) + "\n")
    bc.emit("sdl-driver.rsp", "\n".join([*options, *DRIVER_OPTIONS]) + "\n")


def public_headers(source, config):
    headers  = [(header.name, header) for header in (source / "include/SDL3").glob("*.h")]
    headers += [("SDL_revision.h", header) for header in config.rglob("SDL_revision.h")]
    return headers


def include_layer(source, headers):
    bc.emit("SDL_internal.h", f'#pragma once\n#include "{source / "src/SDL_internal.h"}"\n')
    link = bc.output_dir() / "src"
    if not link.is_symlink() or link.readlink() != source / "src":
        link.unlink(missing_ok=True)
        link.symlink_to(source / "src", target_is_directory=True)
    for name, header in headers:
        bc.emit_bytes("SDL3/" + name, header.read_bytes())


def installed_headers(source, headers):
    bc.emit_bytes("headers.install/share/licenses/SDL3/LICENSE.txt",
                  (source / "LICENSE.txt").read_bytes(), shared=True)
    for name, header in headers:
        bc.emit_bytes("headers.install/include/SDL3/" + name, header.read_bytes(), shared=True)


def operation_header():
    names   = (ROOT / "rdp/SDL_rdpoperations.txt").read_text().splitlines()
    enum    = ",\n  ".join(name.upper() for name in names)
    types   = ",\n      ".join(f"decltype(&sdlrdp_{name})" for name in names)
    symbols = ",\n      ".join(f'"sdlrdp_{name}"' for name in names)
    header  = ("#pragma once\n#include <array>\n#include <tuple>\nnamespace rdp {\n"
               f"enum class Operation {{ {enum}, COUNT }};\n"
               "struct BackendCatalog {\n"
               f"  using Symbols = std::tuple<{types}>;\n"
               f"  static constexpr auto Names = std::to_array<char const*>({{{symbols}}});\n"
               "};\n}\n")
    bc.emit("SDL_rdpoperations.generated.hpp", header)


def main():
    release = pinned_release()
    release.root.mkdir(parents=True, exist_ok=True)
    bc.depends(__file__, VERSIONS, PATCH, ROOT / "rdp/SDL_rdpoperations.txt")
    operation_header()
    source_hash = release.sha256 + digest(PATCH)
    source      = patched_source(release, source_hash)
    config      = configure(release, source, source_hash + digest(Path(__file__)))
    entries     = c_entries(config)
    for entry in entries:
        bc.declare(entry["file"])
    build_config(config, next(entry for entry in entries if Path(entry["file"]).name == "SDL.c"))
    option_files(uniform_options(entries))
    headers = public_headers(source, config)
    include_layer(source, headers)
    bc.emit("exports.map", (source / "src/dynapi/SDL_dynapi.sym").read_text())
    bc.soversion(0)
    installed_headers(source, headers)


if __name__ == "__main__":
    main()
