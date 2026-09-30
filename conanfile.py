"""Conan recipe for sdl-rdp — written once by `buildutil init`, then
project-owned.

Source of truth for required packages is sources/CMakeLists.txt — the
Require(...) calls are parsed here so both the cmake build and the
conan graph agree on versions. Whether this project itself SHIPS as a
conan package is read from buildutil.toml's [package] section (kind =
library|application, name) — the same committed fact the
driver's `buildutil publish` acts on. Without it the recipe is a plain
consumer, exactly as before.
"""

from __future__ import annotations

import fnmatch
import os
import json
import shutil
from pathlib import Path

from conan import ConanFile
from conan.errors import ConanException, ConanInvalidConfiguration
from conan.tools.build import cross_building
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.scm import Version

# buildutil: the Require() parser, buildutil/requires.py verbatim, sha256 9e889b75f7168814; `buildutil init` rewrites this block
"""The Require() lines of sources/CMakeLists.txt, for buildutil and conanfile.py."""

import hashlib
import re
from pathlib import Path

REQUIRE_RE = re.compile(
  # Package name permits letters, digits, underscores, and hyphens
  # (yaml-cpp et al. aren't \w-only).
  r'^\s*Require\s*\(\s*([\w-]+)\s+VERSION\s+"([^"]+)"((?:"[^"]*"|[^")])*)\)',
  re.MULTILINE | re.DOTALL,
)
# One cmake argument; a quoted one keeps its spaces and parentheses.
ARGUMENT_RE = re.compile(r'"([^"]*)"|([^\s"]+)')
QUOTED_OR_COMMENT_RE = re.compile(r'("[^"]*")|#[^\n]*')
REQUIRE_START_RE = re.compile(r'^[ \t]*(Require)\s*\(', re.MULTILINE)


FLAGS = {"TEST": "test", "BENCH": "bench", "TOOL": "tool", "SYSTEM": "system",
         "FORCE": "force", "PUBLIC": "public"}
LISTS = {"CONAN": "conan", "COMPONENTS": "components", "PLATFORM": "platform",
         "OPTIONS": "options"}
KEYWORDS = FLAGS.keys() | LISTS.keys()


def _coerce_option(value: str):
  """A conan option value as its natural type: bool, int, else the text."""
  if value in ("True", "False"):
    return value == "True"
  try:
    return int(value)
  except ValueError:
    return value


def _keyword_values(tokens: list[str]) -> dict[str, list[str]]:
  """Each keyword's value tokens, in exact case as cmake_parse_arguments reads them."""
  values, current = {}, None
  for token in tokens:
    if token in KEYWORDS:
      current = token
      values.setdefault(token, [])
    elif current in LISTS:
      values[current].append(token)
  return values


def _options(tokens: list[str]) -> dict:
  """OPTIONS key=value tokens as a dict."""
  options = {}
  for token in tokens:
    key, eq, value = token.partition("=")
    if not eq or not key:
      # a dropped typo would build the package with the wrong defaults
      raise ValueError(f"Require OPTIONS token {token!r} is not key=value")
    options[key] = _coerce_option(value)
  return options


def arguments(extra: str) -> list[str]:
  """The cmake arguments of a Require call's text, quotes removed."""
  return [quoted or bare for quoted, bare in ARGUMENT_RE.findall(extra)]


def parse_extra(extra: str) -> dict:
  """The keywords after Require(NAME VERSION "v") as one record."""
  values = _keyword_values(arguments(extra))
  out = {field: keyword in values for keyword, field in FLAGS.items()}
  components = values.get("COMPONENTS", [])
  out["conan"] = next(iter(values.get("CONAN", [])), None)
  out["components"] = [token for token in components if token[:1] not in "+-"]
  out["platform"] = values.get("PLATFORM", [])
  out["options"] = _options(values.get("OPTIONS", []))
  _apply_sugar(out, [token for token in components if token[:1] in "+-"])
  if out["force"] and not out["system"]:
    raise ValueError(
      "Require FORCE without SYSTEM: FORCE takes the host's package over "
      "the conan pins it replaces, and only a SYSTEM dep is the host's")
  return out


def _apply_sugar(out: dict, sugar: list) -> None:
  """COMPONENTS +name / -name tokens as the with_name / without_name options they mean."""
  signs = {}
  for token in sugar:
    sign, name = token[0], token[1:]
    if not name:
      raise ValueError(
        f"Require COMPONENTS token {token!r} names no option")
    if signs.setdefault(name, sign) != sign:
      raise ValueError(
        f"Require COMPONENTS has both '+{name}' and '-{name}'")
    for spelling in (f"with_{name}", f"without_{name}"):
      if spelling in out["options"]:
        raise ValueError(
          f"Require COMPONENTS {token!r} and OPTIONS "
          f"{spelling}={out['options'][spelling]!r} both set an option "
          f"for {name!r}")
    # whether the recipe has the option is conan's check, by name
    out["options"]["with_" + name if sign == "+" else "without_" + name] = True
  if sugar and out["system"]:
    raise ValueError(
      "Require COMPONENTS option shorthand with SYSTEM is meaningless — "
      "a SYSTEM dep is the host's package and conan never builds it")


def to_conan_version(v: str) -> str:
  v = v.strip()
  if v == "*":
    return "[*]"
  ops = (">=", "<=", ">", "<", "~", "^")
  if not any(v.startswith(op) for op in ops):
    return v
  # the major cap keeps "cci.20210126"-style versions, which sort above releases, out of range
  m = re.match(r"^[><=~^]+\s*(\d+)\.", v)
  if m:
    major = int(m.group(1))
    return f"[{v} <{major + 1}]"
  return f"[{v}]"


def strip_comments(text: str) -> str:
  """cmake's text with each # line comment outside quotes removed."""
  return QUOTED_OR_COMMENT_RE.sub(lambda match: match.group(1) or "", text)


def _calls(text: str, path: str) -> list[tuple[str, str, str]]:
  """(name, version, extra) of every Require call."""
  calls = []
  for start in REQUIRE_START_RE.finditer(text):
    call = REQUIRE_RE.match(text, start.start())
    if call is None:
      # a skipped call would drop its package unnoticed
      line = text.count("\n", 0, start.start(1)) + 1
      raise ValueError(
        f"{path}:{line}: this Require call does not read as "
        'Require(NAME VERSION "v" ...) with balanced quotes')
    calls.append(call.groups())
  return calls


def requires(text: str, target_os: str | None = None,
             path: str = "sources/CMakeLists.txt") -> list[dict]:
  """The Require() calls active on target_os, or on any platform if None."""
  entries = []
  for name, version, extra in _calls(strip_comments(text), path):
    info = parse_extra(extra)
    if target_os and info["platform"] and target_os not in info["platform"]:
      continue
    conan = info["conan"] or name.lower()
    entries.append({
      "cmake_name": name,
      "conan_name": conan,
      "version": to_conan_version(version),
      "floor": version,
      "ref": (f"{conan}/system@host" if info["system"]
              else f"{conan}/{to_conan_version(version)}"),
      **info,
    })
  return entries


def wrapper_recipe(root: Path, conan: str) -> Path:
  """Where the driver renders conan's system@host recipe for a project."""
  return root / "_bdudata" / "host" / conan / "conanfile.py"


def recipe_revision(recipe: bytes) -> str:
  """conan's hash revision of an export holding only this conanfile.py."""
  summary = f"conanfile.py: {hashlib.md5(recipe).hexdigest()}\n"
  return hashlib.md5(summary.encode()).hexdigest()
# end of the Require() parser


def _package_section() -> dict:
  """[package] from buildutil.toml beside this file; {} = consumer-only.
  A kind of "none" records "the wizard asked, the answer was no"."""
  toml = Path(__file__).resolve().parent / "buildutil.toml"
  if not toml.is_file():
    return {}
  import tomllib
  section = tomllib.loads(toml.read_text(encoding="utf-8")).get("package", {})
  return section if section.get("kind") in ("library", "application") else {}


_PKG = _package_section()

_HOST_REQUIRES = 1
_CONSUMER_OPTIONS = 1

# the conan-center convention for the notices a redistributor must ship
_LICENSE_FILES = ("LICENSE", "LICENSE.*", "LICENCE", "LICENCE.*", "COPYING*",
                  "NOTICE*")


def _parse_requires(recipe_folder: Path, target_os: str,
                    host_packages: bool = True) -> list[dict]:
  """The Require() calls active on target_os, SYSTEM ones if host_packages."""
  text = (recipe_folder / "sources" / "CMakeLists.txt").read_text()
  return [entry for entry in requires(text, target_os)
          if host_packages or not entry["system"]]


def _test_lanes() -> bool:
  """Whether TEST and BENCH lines enter the graph; --no-tests drops them."""
  return os.environ.get("SDL_RDP_SKIP_TEST_DEPS") != "1"


def _held_options(recipe_folder: Path, target_os: str) -> list[dict]:
  """The active lines in the graph whose OPTIONS are consumer patterns."""
  return [entry for entry in _parse_requires(recipe_folder, target_os)
          if entry["options"] and not entry["system"] and not entry["tool"]
          and (_test_lanes() or not (entry["test"] or entry["bench"]))]


def _lost_options(conanfile) -> list[tuple[dict, str, object, object]]:
  """(entry, key, declared, resolved) for each held option the graph changed."""
  lost = []
  for entry in _held_options(Path(conanfile.recipe_folder),
                             str(conanfile.settings.os)):
    if entry["conan_name"] not in conanfile.dependencies:
      continue
    resolved = conanfile.dependencies[entry["conan_name"]].options
    lost += [(entry, key, declared, resolved.get_safe(key))
             for key, declared in entry["options"].items()
             if str(resolved.get_safe(key)) != str(declared)]
  return lost


def _is_root_build(conanfile) -> bool:
  """Whether this recipe is the project the driver builds, not a dependency."""
  root = os.environ.get("BUILDUTIL_CONSUMER")
  return bool(root) and Path(root).resolve() == Path(conanfile.recipe_folder).resolve()


def _lost_refusal(project: str, entry: dict, key: str, declared, got) -> str:
  """Why the project's own build lost an option it declares."""
  cmake, conan = entry["cmake_name"], entry["conan_name"]
  stated = f"{project}: Require({cmake}) sets {conan} option {key}={declared}"
  if got is None:
    return (f"{stated}, and {conan} has no option {key} in this configuration: "
            "its recipe lacks it or removes it here (as fPIC under shared=True "
            "or on Windows). Fix the name, or narrow the line with PLATFORM.")
  return (f"{stated}, and the graph resolved {key}={got}: the profile or the "
          "command line sets it otherwise, or conanfile.py does not set it as "
          "a consumer pattern (`buildutil init` regenerates it, the old recipe "
          "is kept as conanfile.py.bak).")


def _check_options(conanfile) -> None:
  """Refuse a lost option in the root build; as a dependency, the consumer's
  value wins and is reported."""
  lost = _lost_options(conanfile)
  if lost and _is_root_build(conanfile):
    raise ConanException("\n".join(_lost_refusal(conanfile.name, *item)
                                    for item in lost))
  for entry, key, declared, got in lost:
    conanfile.output.warning(
      f"{conanfile.name}: Require({entry['cmake_name']}) sets "
      f"{entry['conan_name']} option {key}={declared}; a consumer downstream "
      f"resolved {key}={got}, which wins.")


def _pin_satisfied(pin: str, host: str) -> bool:
  """An exact pin is met at or above it in its major, a [range] inside it."""
  if pin.startswith("[") and pin.endswith("]"):
    return Version(host).in_range(pin[1:-1])
  return (Version(host) >= Version(pin)
          and Version(host).major == Version(pin).major)


def _host_findings(project: str, entry: dict, resolved: str, host: str,
                   pins: list[tuple[str, str]]) -> tuple[list[str], list[str]]:
  """(refusals, warnings) for one SYSTEM entry against the pins it replaced."""
  cmake, conan = entry["cmake_name"], entry["conan_name"]
  if resolved != entry["ref"]:
    return ([f"{project}: {resolved} replaced the host's {cmake}: a "
             f"requirement downstream pinned {conan} and beat "
             f"Require({cmake} ... SYSTEM). Drop that pin, or depend on "
             f"{project} without it."], [])
  if not _pin_satisfied(entry["version"], host):
    return ([f"{project}: the host's {cmake} is {host}, below this project's "
             f'Require({cmake} VERSION "{entry["floor"]}"). '
             "Upgrade the host."], [])
  unmet = [(owner, pin) for owner, pin in sorted(set(pins))
           if not _pin_satisfied(pin.partition("/")[2], host)]
  if entry["force"]:
    return [], [f"{cmake}: the host's {host} is forced over {owner}'s pin "
                f"{pin} (FORCE)" for owner, pin in unmet]
  return [f"{project}: {owner} pins {pin}; the host's {cmake} is {host} "
          f"(Require({cmake} ... SYSTEM) makes the host's win). Lower the "
          f"pin in {owner.partition('/')[0]}, upgrade the host, or add FORCE "
          "to take the host's anyway." for owner, pin in unmet], []


def _cross_findings(project: str, entry: dict, resolved: str) -> list[str]:
  """A cross build takes nothing from the host, so conan's copy is refused."""
  cmake = entry["cmake_name"]
  return [f"{project}: {resolved} is in this cross build's graph, but "
          f"Require({cmake} ... SYSTEM) takes {cmake} from the target's own "
          "environment; a cross build cannot force the host's over it. "
          f"Drop the pin, or split the line by PLATFORM."]


def _replaced_pins(conanfile, conan: str) -> list[tuple[str, str]]:
  """(owner, pin) for every requirement of conan a force replaced."""
  pins = []
  for dependency in conanfile.dependencies.values():
    for requirement, _ in dependency.dependencies.items():
      replaced = requirement.overriden_ref
      if (requirement.ref.name == conan and replaced is not None
          and replaced.user != "host"):
        pins.append((f"{dependency.ref.name}/{dependency.ref.version}",
                     f"{replaced.name}/{replaced.version}"))
  return pins


def _entry_findings(conanfile, entry: dict) -> tuple[list[str], list[str]]:
  """_host_findings for one SYSTEM entry, read off the resolved graph."""
  found = conanfile.dependencies[entry["conan_name"]]
  resolved = f"{found.ref.name}/{found.ref.version}" + (
    f"@{found.ref.user}" if found.ref.user else "")
  if cross_building(conanfile):
    return _cross_findings(conanfile.name, entry, resolved), []
  return _host_findings(conanfile.name, entry, resolved,
                        str(found.options.get_safe("host_version")),
                        _replaced_pins(conanfile, entry["conan_name"]))


def _in_graph(conanfile, entry: dict) -> bool:
  """A runtime SYSTEM entry is in a native graph; any may be in a cross one."""
  present = entry["conan_name"] in conanfile.dependencies
  lane = entry["test"] or entry["bench"] or cross_building(conanfile)
  if not present and not lane:
    raise ConanException(
      f"{conanfile}: Require({entry['cmake_name']} ... SYSTEM) forced "
      f"{entry['ref']}, and the resolved graph has no {entry['conan_name']}")
  return present


def _dependency_targets(dependency) -> dict[str, str]:
  """One dependency's cmake targets, defaults and declared, as <conan>::<component>."""
  name, info = dependency.ref.name, dependency.cpp_info
  infos = [(name, info), *info.components.items()]
  targets = {f"{name}::{component}": f"{name}::{component}"
             for component, _ in infos}
  targets.update({part.get_property("cmake_target_name"): f"{name}::{component}"
                  for component, part in infos
                  if part.get_property("cmake_target_name")})
  targets.update(info.get_property("buildutil_host_targets") or {})
  return targets


def _linked_targets(conanfile) -> dict[str, str]:
  """Every cmake target the direct dependencies declare, conan-spelled."""
  targets = {}
  for dependency in conanfile.dependencies.direct_host.values():
    targets.update(_dependency_targets(dependency))
  return targets


def _wrapper_ref(recipe_folder: Path, entry: dict) -> str:
  """The wrapper pinned to the revision this checkout's driver rendered; a
  recipe in the cache has no rendering and leaves the pin to its consumer."""
  rendered = wrapper_recipe(recipe_folder, entry["conan_name"])
  if not rendered.is_file():
    return entry["ref"]
  return f"{entry['ref']}#{recipe_revision(rendered.read_bytes())}"


def _wrapper_requests(conanfile) -> dict[str, list[tuple[str, dict]]]:
  """Every recipe's options on each host wrapper it requires, by wrapper."""
  requesters = [(str(conanfile.ref), conanfile.dependencies)] + [
    (str(dependency.ref), dependency.dependencies)
    for dependency in conanfile.dependencies.host.values()]
  requests = {}
  for requester, dependencies in requesters:
    for requirement, _ in dependencies.direct_host.items():
      if requirement.ref.user == "host" and requirement.options:
        requests.setdefault(requirement.ref.name, []).append(
          (requester, requirement.options))
  return requests


def _request_view(options: dict, used: set[str]) -> tuple[str, str]:
  """The components asked for, and the packages entries the wrapper reads."""
  entries = [entry for entry in str(options.get("packages", "")).split()
             if entry.partition("=")[2].partition(":")[0] in used]
  return str(options.get("components", "")), " ".join(sorted(entries))


def _cmake_name(wrapper: str, requests: list[tuple[str, dict]]) -> str:
  """The Require() name a wrapper stands for, from the packages entries."""
  names = [entry.partition("=")[0] for _, options in requests
           for entry in str(options.get("packages", "")).split()
           if entry.partition("=")[2].partition(":")[0] == wrapper]
  return next(iter(names), wrapper)


def _widen_advice(cmake: str, first: tuple, second: tuple) -> str:
  """Which Require line to widen so two requests of one wrapper agree."""
  (asker, (asked, _)), (other, (wanted, _)) = first, second
  if asked == wanted:
    return ("Declare the SYSTEM Require of every package this wrapper finds "
            f"the same way in {asker} and {other}")
  mine, theirs = set(asked.split()), set(wanted.split())
  narrower = asker if mine < theirs else other if theirs < mine else \
    f"{asker} and {other}"
  union = " ".join(sorted(mine | theirs))
  return f"Widen Require({cmake} ... SYSTEM COMPONENTS {union}) in {narrower}"


def _option_conflicts(conanfile) -> list[str]:
  """Two recipes asking one host wrapper for different options: one wins
  silently in conan, so each such pair is a refusal."""
  refusals = []
  for wrapper, requests in _wrapper_requests(conanfile).items():
    used = {dependency.ref.name for dependency in
            conanfile.dependencies[wrapper].dependencies.direct_host.values()}
    views = [(asker, _request_view(options, used)) for asker, options in requests]
    first = views[0]
    for second in views[1:]:
      if second[1] != first[1]:
        refusals.append(
          f"{wrapper}/system@host: {first[0]} asks components={first[1][0]!r} "
          f"packages={first[1][1]!r}, {second[0]} asks "
          f"components={second[1][0]!r} packages={second[1][1]!r}; one wrapper "
          "serves the whole graph and an option holds one value. "
          f"{_widen_advice(_cmake_name(wrapper, requests), first, second)}.")
  return refusals


def _refuse_foreign_copies(conanfile) -> None:
  """Refuse a SYSTEM package not the host's, or below a pin it replaced."""
  entries = _parse_requires(Path(conanfile.recipe_folder),
                            str(conanfile.settings.os))
  refusals = []
  for entry in entries:
    if not entry["system"] or not _in_graph(conanfile, entry):
      continue
    refused, warned = _entry_findings(conanfile, entry)
    refusals += refused
    for line in warned:
      conanfile.output.warning(line)
  refusals += _option_conflicts(conanfile)
  if refusals:
    # ConanException, not ConanInvalidConfiguration: conan reports an invalid
    # consumer only after building every dependency, this stops the graph.
    raise ConanException("\n".join(refusals))


class ProjectRecipe(ConanFile):
  name = _PKG.get("name", "sdl-rdp")
  settings = "os", "compiler", "build_type", "arch"
  if _PKG:
    package_type = {"library": "library",
                    "application": "application"}[_PKG["kind"]]
    if _PKG["kind"] == "library":
      # the conan-standard shared option, linked to buildutil's
      # module_linkage config: the driver passes -o &:shared=True when
      # the config says shared, and conan resolves the "library" type
      # (and distinct package_ids) from it
      options = {"shared": [True, False]}
      default_options = {"shared": False}
    # exports ride WITH the recipe into the cache — the cached copy
    # still reads its [package] section and parses the Require() calls
    # at graph time (exports_sources only materialize for builds)
    exports = ("buildutil.toml", "sources/CMakeLists.txt")
    # THE EXCLUSIONS ARE THE POINT OF THE SECOND LINE. sources/* goes in
    # wholesale, and conan hashes what it exports into the RECIPE
    # REVISION -- so one generated file that exists on one machine and
    # not another splits a release across two revisions, and consumers
    # resolve only the latest one, which hides every binary published
    # under the other. That is not hypothetical: one package
    # shipped a second revision from its Windows runner whose manifest
    # differed by five cmake_test_discovery_<hash>.json files (cmake 4.2
    # wrote them into the test's working directory, which was the source
    # tree -- fixed in the machinery, this is the belt to that brace),
    # and __pycache__ under sources/ is the same class of accident, which
    # is why every publish job also sets PYTHONDONTWRITEBYTECODE.
    #
    # A `!pattern` entry is conan's own exclusion syntax and is evaluated
    # after the includes.
    exports_sources = ("CMakeLists.txt", "buildutil.toml", "sources/*",
                       "cmake/*", ".buildutil/*", *_LICENSE_FILES,
                       "!sources/**/cmake_test_discovery_*.json",
                       "!sources/**/__pycache__/**",
                       "!sources/**/*.pyc")

  def set_version(self):
    # ONE derivation, and the version never lives in the source: the
    # driver computes it (last semver git tag + publish build number,
    # or its --version switch) and passes --version here; adopt it.
    # 0.0.0 is the consumer-only placeholder nothing ever publishes.
    self.version = self.version or "0.0.0"

  def validate(self):
    # this project is controlled by buildutil, which exports BUILDUTIL=
    # <version> to every child it drives — a bare `conan install .`
    # has no such parent and would resolve a graph outside the driver's
    # profile/venv contracts
    if not os.environ.get("BUILDUTIL"):
      raise ConanInvalidConfiguration(
        "this project is controlled by buildutil — run `buildutil build` "
        "(conan is orchestrated: profile, CONAN_HOME and the dependency "
        "graph all come from the driver). If you really need direct "
        "conan, set BUILDUTIL=1 in the environment.")
    _refuse_foreign_copies(self)
    _check_options(self)

  def layout(self):
    cmake_layout(self)
    profile = _profile_name(self.settings)
    self.folders.build = f"_build/{profile}"
    self.folders.generators = f"_build/{profile}/generators"

  def generate(self):
    CMakeToolchain(self).generate()
    CMakeDeps(self).generate()

  def configure(self):
    # a dependency requiring the package first fixes its options; a consumer pattern wins
    for entry in _held_options(Path(self.recipe_folder), str(self.settings.os)):
      for key, value in entry["options"].items():
        setattr(self.options[f"{entry['conan_name']}/*"], key, value)

  def requirements(self):
    entries = _parse_requires(Path(self.recipe_folder), str(self.settings.os),
                              not cross_building(self))
    packages = " ".join(
      f"{entry['cmake_name']}={entry['conan_name']}"
      + (":" + ",".join(entry["components"]) if entry["components"] else "")
      for entry in entries if entry["system"])
    for entry in entries:
      if not entry["tool"]:                # build_requirements() owns these
        self._require(entry, packages)

  def _require(self, entry: dict, packages: str) -> None:
    """One Require() line as its conan requirement."""
    # equal to configure()'s patterns, so conan sees no options conflict
    kwargs = {"options": entry["options"]} if entry["options"] else {}
    # PUBLIC: this dep's headers appear in headers this package
    # exports, so consumers must see them too — conan does not
    # propagate a static-lib requirement's headers by default.
    if entry["public"]:
      kwargs["transitive_headers"] = True
    # BENCH deps follow TEST's test-only semantics (conan has no
    # bench_requires); a test-only SYSTEM overrides without entering the
    # runtime graph.
    if entry["test"] or entry["bench"]:
      if _test_lanes():
        if entry["system"]:
          self.requires(_wrapper_ref(Path(self.recipe_folder), entry),
                        override=True)
        else:
          self.test_requires(entry["ref"], **kwargs)   # --no-tests drops these
    elif entry["system"]:
      kwargs["options"] = {"components": " ".join(entry["components"]),
                           "packages": packages}
      self.requires(_wrapper_ref(Path(self.recipe_folder), entry), force=True,
                    **kwargs)
    else:
      self.requires(entry["ref"], **kwargs)

  def build_requirements(self):
    target_os = str(self.settings.os)
    for entry in _parse_requires(Path(self.recipe_folder), target_os,
                                 host_packages=False):
      if entry["tool"]:
        kwargs = {"options": entry["options"]} if entry["options"] else {}
        self.tool_requires(entry["ref"], **kwargs)

  # ---- packaging (active only with [package] in buildutil.toml) ----
  # The flow is export-pkg-based: `buildutil publish` (and the package
  # half of `buildutil test`) builds the tree as always, then packages
  # THE LOCAL BUILD via package() below. A cache source-build (a
  # consumer's --build=missing) works when the package carries its own
  # driver (`publish --bake-buildutil`, or any vendored project —
  # exports_sources ships .buildutil/*); without it the build is
  # refused with an explanation rather than half-attempted.

  def build(self):
    if not _PKG:
      return
    import sys
    # folders may be unset on a barely-constructed recipe (tests, a
    # bare export) — the refusal below must fire, not an AttributeError
    source = Path(getattr(self, "source_folder", None)
                  or getattr(self, "recipe_folder", None) or ".")
    vendored = source / ".buildutil"
    if (vendored / "buildutil" / "__main__.py").is_file():
      # the baked lane: conan has already resolved this package's own
      # Require()s against the CONSUMER's cache/remotes and generated
      # the toolchain — the vendored buildutil only supplies its cmake
      # machinery and configure contract (stdlib-only `cache-build`:
      # no venv, no nested conan, no network in the cache)
      shared = self.options.get_safe("shared")
      toolchain = Path(self.generators_folder) / "conan_toolchain.cmake"
      previous = os.environ.get("PYTHONPATH")
      os.environ["PYTHONPATH"] = (
        f"{vendored}{os.pathsep}{previous}" if previous else str(vendored))
      try:
        self.run(
          f'"{sys.executable}" -m buildutil cache-build'
          f' --build-dir "{self.build_folder}"'
          f' --toolchain "{toolchain}"'
          f' --build-type {self.settings.build_type}'
          f' --package-version {self.version}'
          f' --linkage {"shared" if shared else "static"}',
          cwd=str(source))
      finally:
        if previous is None:
          os.environ.pop("PYTHONPATH", None)
        else:
          os.environ["PYTHONPATH"] = previous
      return
    # version/settings may be unset (tests, a bare export — settings is
    # still the class-level tuple until conan populates it); the story
    # is worth telling either way, with "?" for what is missing
    version = getattr(self, "version", None)
    ref = f"{self.name}/{version}" if version else self.name
    settings = getattr(self, "settings", None)
    def _setting(name):
      return settings.get_safe(name) if hasattr(settings, "get_safe") else "?"
    profile = (f"build_type={_setting('build_type')}, "
               f"compiler={_setting('compiler')}-{_setting('compiler.version')}, "
               f"cppstd={_setting('compiler.cppstd')}")
    raise ConanException(
      f"{self.name}: building this package from source inside the conan "
      "cache is not supported — it was published WITHOUT its build "
      "driver baked in (`buildutil publish --bake-buildutil` changes "
      "that), so binaries come from the project remote, which publish "
      "keeps populated. Fetch a prebuilt binary, or clone the project "
      "and run `buildutil publish` for your profile.\n"
      f"You are here because no published binary matched your profile: "
      f"{profile}.\n"
      "SEE WHAT IS ACTUALLY PUBLISHED FIRST — it settles this in one "
      f"command:\n    conan list \"{ref}:*\" -r <remote>\n"
      "If the published list simply has no entry for your build_type "
      "(publishing Release but not Debug, or the reverse, is the common "
      "case), the fix is on the PUBLISHER: run `buildutil publish` for "
      "the missing profile. Nothing is wrong on your side.\n"
      "Only if your build_type IS published does the package_id-drift "
      "explanation apply: a version-RANGED dependency of this "
      "package resolved differently in your cache than at publish time. "
      "Compare `conan graph info` resolutions against the published "
      "package's requires and align them (update/pin the drifting dep) "
      "instead of building from source.")

  def package(self):
    if not _PKG:
      return
    # the layout() above points build_folder at the same
    # _build/<profile> tree buildutil just built — install it whole;
    # the install tree mirrors the source tree (buildutil convention)
    self.run(f'cmake --install "{self.build_folder}" '
             f'--prefix "{self.package_folder}"')
    self._package_licenses()

  def _package_licenses(self) -> None:
    """The root's licence files, links resolved, into licenses/."""
    found = _licence_entries(Path(self.source_folder))
    broken = [path.name for path in found if not path.is_file()]
    for name in broken:
      self.output.warning(f"{self.name}: licence {name} is a link to a missing "
                          "target, so it is not packaged")
    if not found:
      self.output.warning(
        f"{self.name}: no LICENSE, LICENCE, COPYING or NOTICE file at the "
        "project root, so the package carries no licenses/")
    licenses = Path(self.package_folder) / "licenses"
    for path in (path for path in found if path.is_file()):
      licenses.mkdir(parents=True, exist_ok=True)
      shutil.copyfile(path, licenses / path.name)

  def package_info(self):
    if not _PKG:
      return
    root = Path(self.package_folder)
    modules = _cmake_build_modules(root, self.name)
    if modules:
      self.cpp_info.set_property("cmake_build_modules", modules)
    if _PKG["kind"] == "application":
      self._application_info(root)
      return
    libs, libdirs = _shipped_libraries(root)
    incdirs = ["include"] if (root / "include").is_dir() else []
    components = _manifest_components(root)
    # one module is not worth componentising: the aggregate IS the module
    if len(components) > 1:
      self._component_info(components, libs, libdirs, incdirs)
      return
    self.cpp_info.libs = sorted(libs)
    self.cpp_info.libdirs = libdirs
    self.cpp_info.includedirs = incdirs

  def _application_info(self, root: Path) -> None:
    """An application advertises the directories of its executables only."""
    self.cpp_info.bindirs = sorted({
      str(p.parent.relative_to(root)) for p in root.rglob("*")
      if p.is_file() and os.access(p, os.X_OK)}) or ["."]
    self.cpp_info.libdirs = []
    self.cpp_info.includedirs = []

  def _component_info(self, components: list[dict], libs: set[str],
                       libdirs: list[str], incdirs: list[str]) -> None:
    """One conan component per module, its requires from the manifest."""
    by_path = {c["path"]: _component_name(c["path"], self.name)
               for c in components}
    linked = any(c.get("external") or c.get("host") for c in components)
    targets = _linked_targets(self) if linked else {}
    for c in components:
      component = self.cpp_info.components[by_path[c["path"]]]
      component.libs = [c["lib"]] if c["lib"] in libs else []
      component.libdirs = libdirs
      component.includedirs = incdirs
      external = c.get("external", [])
      component.requires = [by_path[n] for n in c.get("needs", [])
                            if n in by_path] + self._external_requires(
        c, targets) + self._host_requires(c, targets)
      component.system_libs = [item for item in external
                               if item not in targets and "::" not in item]

  def _external_requires(self, component: dict,
                         targets: dict[str, str]) -> list[str]:
    """A component's namespaced links as the dependency components they are."""
    namespaced = [t for t in component.get("external", [])
                  if t in targets or "::" in t]
    unresolved = [t for t in namespaced if t not in targets]
    if unresolved:
      raise ConanException(
        f"{self.name}: module {component['path']} links {unresolved}, which "
        "no direct dependency of this package declares as a cmake target; "
        "Require the package that provides it.")
    return [targets[t] for t in namespaced]

  def _host_requires(self, component: dict,
                     targets: dict[str, str]) -> list[str]:
    """A component's host targets as the wrapper components declaring them;
    a cross build forces no wrapper, so its targets are the target's own."""
    if cross_building(self):
      return []
    unresolved = [t for t in component.get("host", []) if t not in targets]
    if unresolved:
      raise ConanException(
        f"{self.name}: module {component['path']} links {unresolved}, which a "
        "SYSTEM find imported and no host wrapper this package requires "
        "declares; link the target of the package's own SYSTEM Require.")
    return [targets[t] for t in component.get("host", [])]


def _licence_entries(root: Path) -> list[Path]:
  """Root entries a licence pattern names, case-insensitively as conan exports."""
  return sorted(path for path in root.iterdir() if not path.is_dir()
                and any(fnmatch.fnmatchcase(path.name.upper(), pattern)
                        for pattern in _LICENSE_FILES))


def _shipped_libraries(root: Path) -> tuple[set[str], list[str]]:
  """The libraries the install mirror shipped, by extension, and their dirs."""
  libs, libdirs = set(), set()
  for p in root.rglob("*"):
    if p.is_file() and (p.suffix in (".a", ".lib", ".so", ".dylib")
                        or ".so." in p.name):
      stem = p.name.split(".")[0]
      libs.add(stem[3:] if stem.startswith("lib") else stem)
      libdirs.add(str(p.parent.relative_to(root)))
  # `[package] linkable = false`: what this package ships is loaded at
  # run time, not linked (libretro cores, plugins, a data payload); it
  # keeps its libdirs for the loader and advertises no link target.
  if not _PKG.get("linkable", True):
    libs = set()
  return libs, sorted(libdirs) or ["lib"]


def _manifest_components(root: Path) -> list[dict]:
  """The module graph the build wrote; Link_dependencies is cmake and this
  recipe cannot see it."""
  manifest = root / "share" / "buildutil" / "buildutil-components.json"
  if not manifest.is_file():
    return []
  try:
    return json.loads(manifest.read_text()).get("components", [])
  except ValueError:
    return []


def _component_name(path: str, package: str) -> str:
  """sources/-relative path as a component: sources/<pkg>/net/http is
  net-http, so the target reads <pkg>::net-http, not <pkg>::<pkg>-net-http."""
  parts = [x for x in path.split("/") if x]
  if parts and parts[0] == package:
    parts = parts[1:]
  return "-".join(parts) or package


def _cmake_build_modules(root: Path, name: str) -> list[str]:
  """`share/cmake/<package>/*.cmake` in the install tree IS a cmake build
  module: a file put there exists to be include()d by find_package, and
  nothing else puts one there. Presence is the declaration, as it is for
  every other directory convention -- a project generates the file into
  its `*.install/` tree and this announces it."""
  directory = root / "share" / "cmake" / name
  return [str(found.relative_to(root))
          for found in sorted(directory.glob("*.cmake"))]


def _module_linkage() -> str:
  """The module_linkage config option, resolved exactly as the driver
  resolves it (env — which the driver exports pre-resolved — then the
  checkout-local ini, then static). layout() needs it because the
  profile DIR carries the linkage axis: static and shared are separate
  build trees, and this recipe must point at the same one the driver
  builds."""
  env = os.environ.get("BUILDUTIL_OPT_MODULE_LINKAGE", "")
  if env in ("static", "shared"):
    return env
  ini = Path(__file__).resolve().parent / "_bdudata" / "config.ini"
  if ini.is_file():
    section = ""
    for raw in ini.read_text().splitlines():
      line = raw.split("#", 1)[0].strip()
      if not line:
        continue
      if line.startswith("[") and line.endswith("]"):
        section = line[1:-1].strip().lower()
      elif section == "options":
        key, _, value = line.partition("=")
        if key.strip() == "module_linkage" and value.strip() in (
            "static", "shared"):
          return value.strip()
  return "static"


def _profile_name(settings) -> str:
  parts = [str(settings.arch), str(settings.os), str(settings.compiler)]
  linkage = _module_linkage()
  if linkage != "static":
    parts.append(linkage)
  parts.append(str(settings.build_type))
  return "-".join(parts).lower()
