"""Build SDL from its release archive with the rdp driver patched in, configured by SDL's own CMake."""
import hashlib
import json
import re
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
# NDEBUG and DEBUG follow the build type of the build that compiles SDL, never SDL's own configure.
BUILD_TYPE_DEFINES = frozenset(("NDEBUG", "DEBUG", "_DEBUG"))
OPTION_PREFIX      = "-W"
OPTION_FLAGS       = frozenset(("-pthread", "-fno-strict-aliasing", "-fno-strict-overflow"))
DRIVER_OPTIONS     = ("-Wsign-compare",)
FRAMEWORK_FLAGS    = {"-framework": False, "-weak_framework": True}
# -pthread and -lpthread in LINK_FLAGS are the outer build's threads policy; its other flags are how SDL links.
THREAD_LIBRARIES   = frozenset(("-lpthread",))
SHARED_LINK        = re.compile(r"^build [^\n]*\b(C|CXX)_SHARED_LIBRARY_LINKER__SDL3-shared_[^\n]*\n((?:  [^\n]*\n)*)",
                                re.MULTILINE)
VARIABLES          = re.compile(r"^  (\w+) = (.*)$", re.MULTILINE)
BUILD_OUTPUTS      = re.compile(r"^build ([^:\n]+):", re.MULTILINE)
TOOLCHAIN_FILE     = "-DCMAKE_TOOLCHAIN_FILE="
FRONTEND_VARIANT   = re.compile(r'^set\(CMAKE_C_COMPILER_FRONTEND_VARIANT "(\w*)"\)$', re.MULTILINE)


@dataclass(frozen=True)
class Options:
    enabled:  tuple[str, ...] = ()
    disabled: tuple[str, ...] = ()
    cmake:    tuple[str, ...] = ()

    def __add__(self, other):
        return Options(self.enabled + other.enabled, self.disabled + other.disabled, self.cmake + other.cmake)

    def arguments(self):
        return [*(f"-DSDL_{option}=ON" for option in self.enabled),
                *(f"-DSDL_{option}=OFF" for option in self.disabled), *self.cmake]


COMMON  = Options(("RDP", "RDPAUDIO", "RDPSTORAGE", "SHARED"),
                  ("OPENGL", "OPENGLES", "VULKAN", "GPU", "HIDAPI", "CAMERA", "TRAY", "DIALOG", "TESTS", "EXAMPLES",
                   "STATIC", "TEST_LIBRARY"))
# UNIX_CONSOLE_BUILD waives SDL's X11-or-Wayland check, which it makes under UNIX AND NOT APPLE only.
TARGETS = {"Linux":   Options(("UNIX_CONSOLE_BUILD",),
                              ("X11", "WAYLAND", "ALSA", "PULSEAUDIO", "SNDIO", "DBUS", "LIBUDEV")),
           "Windows": Options(),
           # SDL's add_library refuses Ninja's rpath relink on Mach-O.
           "Darwin":  Options(cmake=("-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON",))}


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
    # 2bitsin/buildutil#1: the hook receives no [options], so the release is pinned here.
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
    bc.run(["git", "apply", "-p1", "--check", PATCH], what="checking rdp-driver.patch", cwd=source)
    bc.run(["git", "apply", "-p1", PATCH], what="applying rdp-driver.patch", cwd=source)
    stamp.write_text(fingerprint)
    return source


def target_options(target):
    if target not in TARGETS:
        raise SystemExit(f"SDL is configured for {', '.join(TARGETS)}, not {target}")
    return COMMON + TARGETS[target]


def toolchain_digests(toolchain):
    return [digest(Path(argument.removeprefix(TOOLCHAIN_FILE))) for argument in toolchain
            if argument.startswith(TOOLCHAIN_FILE)]


def configure(source, fingerprint):
    config      = bc.output_dir() / "config"
    stamp       = bc.output_dir() / "configured.sha256"
    target      = bc.target_system()
    toolchain   = bc.cmake_toolchain_args()
    fingerprint = "\n".join((fingerprint, str(source), target, *toolchain, *toolchain_digests(toolchain)))
    if stamp.exists() and stamp.read_text() == fingerprint:
        return config
    shutil.rmtree(config, ignore_errors=True)
    command  = ["cmake", "-S", source, "-B", config, "-G", "Ninja", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    command += [*target_options(target).arguments(), "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON", *toolchain]
    bc.run(command, what="SDL's CMake configure")
    stamp.write_text(fingerprint)
    return config


def arguments(entry):
    return entry.get("arguments") or shlex.split(entry["command"])


def compiled_entries(config):
    return json.loads((config / "compile_commands.json").read_text())


def cache_value(config, name):
    found = re.search(rf"^{name}:[A-Z]+=(.*)$", (config / "CMakeCache.txt").read_text(), re.MULTILINE)
    return found.group(1) if found else ""


def gnu_frontend(config):
    compiler = next(config.glob("CMakeFiles/*/CMakeCCompiler.cmake")).read_text()
    found    = FRONTEND_VARIANT.search(compiler)
    if found is None:
        raise SystemExit(f"{config} records no C compiler frontend variant")
    return found.group(1) == "GNU"


def defines(entry):
    return [argument[2:] for argument in arguments(entry)
            if argument.startswith("-D") and argument[2:].partition("=")[0] not in BUILD_TYPE_DEFINES]


def compile_options(entry):
    return [argument for argument in arguments(entry)
            if argument.startswith(OPTION_PREFIX) or argument in OPTION_FLAGS]


def build_config(config):
    header = next(config.rglob("SDL_build_config.h")).read_text()
    # DEBUG follows the compiling build's NDEBUG, as SDL's own $<CONFIG:Debug> follows its build type.
    lines  = ["", "#ifndef NDEBUG", "#define DEBUG 1", "#endif", ""]
    bc.emit("SDL_build_config.h", header + "\n".join(lines))


def declare_sources(config, entries, sdl_c):
    gnu = gnu_frontend(config)
    for entry in entries:
        bc.declare(entry["file"], options=compile_options(entry) if gnu else [], defines=defines(entry))
    driver = [*compile_options(sdl_c), *DRIVER_OPTIONS] if gnu else []
    for source in sorted(ROOT.rglob("*.cpp")):
        bc.declare(source, options=driver, defines=defines(sdl_c))


def shared_link_statement(ninja, path):
    found     = SHARED_LINK.search(ninja)
    variables = dict(VARIABLES.findall(found.group(2))) if found else {}
    if "LINK_LIBRARIES" not in variables:
        raise SystemExit(f"{path} holds no SDL3-shared link statement with LINK_LIBRARIES")
    return found.group(1), variables


def without_suffix(tokens, suffix):
    return tokens[:-len(suffix)] if suffix and tokens[-len(suffix):] == suffix else tokens


def shared_link_libraries(config):
    ninja               = (config / "build.ninja").read_text()
    language, variables = shared_link_statement(ninja, config / "build.ninja")
    internal            = {output for line in BUILD_OUTPUTS.findall(ninja) for output in line.split()}
    standard            = shlex.split(cache_value(config, f"CMAKE_{language}_STANDARD_LIBRARIES"))
    flags               = [token for token in shlex.split(variables.get("LINK_FLAGS", ""))
                           if token.startswith("-l") and token not in THREAD_LIBRARIES]
    libraries           = without_suffix(shlex.split(variables["LINK_LIBRARIES"]), standard)
    # -Xlinker hands the next token to the linker: -Xlinker -weak_framework -Xlinker X is the pair -weak_framework X.
    return [token for token in [*flags, *libraries] if token != "-Xlinker" and token not in internal]


def library_name(token):
    if token.startswith("-l"):
        return token.removeprefix("-l")
    if not token.startswith("-") and ("/" in token or "\\" in token):
        return token
    if not token.startswith("-") and token.endswith(".lib"):
        return token.removesuffix(".lib")
    raise SystemExit(f"SDL's link line holds {token!r}, which is neither -l<name>, <name>.lib, a path nor a framework")


def declare_links(config):
    tokens = iter(shared_link_libraries(config))
    for token in tokens:
        if token in FRAMEWORK_FLAGS:
            bc.framework(next(tokens), weak=FRAMEWORK_FLAGS[token])
        else:
            bc.link(library_name(token))


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


def main():
    release = pinned_release()
    release.root.mkdir(parents=True, exist_ok=True)
    bc.depends(__file__, VERSIONS, PATCH)
    source_hash = release.sha256 + digest(PATCH)
    source      = patched_source(release, source_hash)
    config      = configure(source, source_hash + digest(Path(__file__)))
    entries     = compiled_entries(config)
    sdl_c       = next(entry for entry in entries if Path(entry["file"]).name == "SDL.c")
    build_config(config)
    declare_sources(config, entries, sdl_c)
    declare_links(config)
    headers = public_headers(source, config)
    include_layer(source, headers)
    bc.emit("exports.map", (source / "src/dynapi/SDL_dynapi.sym").read_text())
    bc.soversion(0)
    installed_headers(source, headers)


if __name__ == "__main__":
    main()
