import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import tomllib
import sys
import urllib.request

import buildutil_configure as bc

ROOT = bc.source_dir()
SDL_VERSION = os.environ["SDL_RDP_VERSION"]
VERSIONS = ROOT / "versions.toml"
SDL_SHA256 = tomllib.loads(VERSIONS.read_text()).get(SDL_VERSION)
if SDL_SHA256 is None:
    sys.exit(f"SDL {SDL_VERSION} is unlisted; extend {VERSIONS}")
SDL_URL = f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL_VERSION}/SDL3-{SDL_VERSION}.tar.gz"
SHARED = bc.output_dir(shared=True) / SDL_VERSION
SHARED.mkdir(parents=True, exist_ok=True)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run(command, **kwargs):
    print(shlex.join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def download():
    archive = SHARED / f"SDL3-{SDL_VERSION}.tar.gz"
    if archive.exists() and digest(archive) == SDL_SHA256:
        print("Download skipped: verified tarball", flush=True)
        return archive
    temporary = archive.with_suffix(".tmp")
    print(f"Downloading {SDL_URL}", flush=True)
    with urllib.request.urlopen(SDL_URL) as source, temporary.open("wb") as target:
        shutil.copyfileobj(source, target)
    if digest(temporary) != SDL_SHA256:
        temporary.unlink()
        raise ValueError("SDL archive SHA256 mismatch")
    temporary.replace(archive)
    return archive


def extract(archive, fingerprint):
    source = SHARED / f"SDL3-{SDL_VERSION}"
    stamp = SHARED / "extracted.sha256"
    if source.is_dir() and stamp.exists() and stamp.read_text() == fingerprint:
        print("Extraction skipped: matching source stamp", flush=True)
        return source
    print(f"Extracting {archive}", flush=True)
    shutil.rmtree(source, ignore_errors=True)
    with tarfile.open(archive) as package:
        package.extractall(SHARED, filter="data")
    stamp.write_text(fingerprint)
    return source


def patch_driver(source):
    command = ["patch", "-p1", "--batch", "-i", ROOT / "rdp-driver.patch"]
    reverse = subprocess.run(list(map(str, command + ["--dry-run", "--force", "-R"])),
                             cwd=source, capture_output=True)
    if reverse.returncode == 0:
        print("Patch skipped: already applied", flush=True)
    else:
        run(command, cwd=source)


def configure(source, fingerprint):
    config = SHARED / "config"
    stamp = SHARED / "configured.sha256"
    if stamp.exists() and stamp.read_text() == fingerprint:
        print("SDL configure skipped: matching configuration stamp", flush=True)
        return config
    disabled = "X11 WAYLAND OPENGL OPENGLES VULKAN GPU ALSA PULSEAUDIO SNDIO DBUS LIBUDEV HIDAPI CAMERA TRAY DIALOG TESTS EXAMPLES"
    command = ["cmake", "-S", source, "-B", config, "-G", "Ninja",
               "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
               f"-DCMAKE_BUILD_TYPE={os.environ['SDL_RDP_BUILD_TYPE']}"]
    command += [f"-DSDL_{option}=OFF" for option in disabled.split()]
    command += ["-DSDL_UNIX_CONSOLE_BUILD=ON", "-DSDL_RDP=ON", "-DSDL_RDPAUDIO=ON", "-DSDL_RDPSTORAGE=ON",
                f"-DCMAKE_C_FLAGS=-I{ROOT / 'rdp'} -I{ROOT.parent}",
                "-DSDL_RDP_DYNAMIC=libsdl-rdp-backend.so", "-DSDL_SHARED=ON",
                "-DSDL_STATIC=OFF", "-DSDL_TEST_LIBRARY=OFF",
                "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON"]
    run(command)
    stamp.write_text(fingerprint)
    return config


def quoted(value):
    return '[=[' + str(value) + ']=]'


def command_settings(entry):
    arguments = entry.get("arguments") or shlex.split(entry["command"])
    includes = [arg[2:] for arg in arguments if arg.startswith('-I')]
    includes += [arg[len('-idirafter'):] for arg in arguments
                 if arg.startswith('-idirafter')]
    defines = [arg[2:] for arg in arguments if arg.startswith('-D')]
    options = [arg for arg in arguments if arg.startswith(('-m', '-W')) or
               arg in ('-pthread', '-fno-strict-aliasing', '-fno-strict-overflow')]
    if Path(entry["file"]).name.startswith("SDL_rdp"):
        options.append("-Wsign-compare")
    return includes, defines, options


def declare_sources(config):
    entries = json.loads((config / "compile_commands.json").read_text())
    entries = [entry for entry in entries if Path(entry['file']).suffix == '.c']
    includes, defines, options = zip(*(command_settings(entry) for entry in entries))
    driver = next(entry for entry in entries if Path(entry["file"]).name == "SDL.c")
    _, driver_defines, driver_options = command_settings(driver)
    driver_options.append("-Wsign-compare")
    common = set.intersection(*map(set, defines))
    lines = ['target_compile_definitions(SDL3 PRIVATE ' +
             ' '.join(map(quoted, sorted(common))) + ')']
    lines += ['target_include_directories(SDL3 PRIVATE ' +
              ' '.join(map(quoted, sorted(set().union(*map(set, includes))))) + ')']
    for entry, definitions, flags in zip(entries, defines, options):
        path = Path(entry['file'])
        bc.declare(path)
        lines += [f'set_source_files_properties({quoted(path)} PROPERTIES '
                  f'COMPILE_DEFINITIONS {quoted(";".join(sorted(set(definitions) - common)))} '
                  f'COMPILE_OPTIONS {quoted(";".join(flags))})']
    lines += [f'set_source_files_properties({quoted(path)} PROPERTIES '
              f'COMPILE_DEFINITIONS {quoted(";".join(driver_defines))} '
              f'COMPILE_OPTIONS {quoted(";".join(driver_options))})'
              for path in sorted((ROOT / "rdp").glob("*.c"))]
    print(f"Declared {len(entries)} SDL C sources", flush=True)
    return lines


def settings(source, config):
    lines = declare_sources(config)
    lines += [f'target_include_directories(SDL3 PRIVATE {quoted(source)} PUBLIC '
              f'"$<BUILD_INTERFACE:{source}/include>" "$<INSTALL_INTERFACE:include>")',
              f'target_link_options(SDL3 PRIVATE "LINKER:--version-script={source}/src/dynapi/SDL_dynapi.sym")',
              f'set_property(TARGET SDL3 APPEND PROPERTY LINK_DEPENDS {quoted(source / "src/dynapi/SDL_dynapi.sym")})']
    ninja = (config / "build.ninja").read_text()
    link = next(line for line in ninja.splitlines() if line.startswith('  LINK_LIBRARIES ='))
    libraries = [word[2:] for word in shlex.split(link.partition('=')[2]) if word.startswith('-l')]
    if '-pthread' in link:
        libraries.append('pthread')
    lines += ['Link_dependencies(' + ' '.join(dict.fromkeys(libraries)) + ')']
    print('SDL system libraries: ' + ' '.join(dict.fromkeys(libraries)), flush=True)
    bc.emit_bytes("headers.install/share/licenses/SDL3/LICENSE.txt",
                  (source / "LICENSE.txt").read_bytes(), shared=True)
    bc.emit("headers.install/share/cmake/sdl-rdp/components.cmake",
            "add_library(sdl-rdp::SDL3 ALIAS sdl-rdp::SDL3.so)\n"
            "add_library(sdl-rdp::sdl-rdp-backend ALIAS sdl-rdp::sdl-rdp-backend.so)\n",
            shared=True)
    bc.emit('settings.cmake', '\n'.join(lines) + '\n')
    for header in (source / 'include/SDL3').glob('*.h'):
        bc.emit_bytes('headers.install/include/SDL3/' + header.name,
                      header.read_bytes(), shared=True)
    for header in config.rglob('SDL_build_config.h'):
        bc.declare(header)
    for header in config.rglob('SDL_revision.h'):
        bc.declare(header)
        bc.emit_bytes('headers.install/include/SDL3/SDL_revision.h',
                      header.read_bytes(), shared=True)


def main():
    print(f'SDL version: {SDL_VERSION}', flush=True)
    bc.depends(__file__, VERSIONS, ROOT / 'rdp-driver.patch',
               ROOT.parent / 'sdl-rdp-backend.so/sdl-rdp-backend.h')
    bc.inputs('*.c', '*.h', root=ROOT / 'rdp')
    source_hash = SDL_SHA256 + digest(ROOT / 'rdp-driver.patch')
    source = extract(download(), source_hash)
    patch_driver(source)
    config_hash = source_hash + digest(Path(__file__)) + os.environ['SDL_RDP_BUILD_TYPE']
    settings(source, configure(source, config_hash))


main()
