"""The namespace lint: declarations in `<folder>::detail::<stem>`, a header re-exporting what it declares, last."""
import subprocess

import pytest

import namespaces

HEADER = 'sources/sdl-rdp/video-codec/frame-rate.hpp'
SOURCE = 'sources/sdl-rdp/video-codec/frame-rate.cpp'
DETAIL = 'namespace sdl_rdp::video_codec::detail::frame_rate {\n'
EXPORT = 'namespace sdl_rdp::video_codec {\n'


@pytest.fixture
def tree(tmp_path):
    subprocess.run(['git', 'init', '-q', str(tmp_path)], check=True)
    for folder in ('video-codec', 'link', 'SDL3/rdp'):
        (tmp_path / 'sources/sdl-rdp' / folder).mkdir(parents=True)
    (tmp_path / 'sources/sdl-rdp/link/peer-link.hpp').write_text('')
    (tmp_path / 'sources/sdl-rdp/video-codec/clock.hpp').write_text('')
    return tmp_path


def findings(tree, text, name=HEADER):
    (tree / name).write_text(text)
    subprocess.run(['git', 'add', '-A'], cwd=tree, check=True)
    return [f'{finding.path.name}:{finding.line}' for finding in namespaces.findings(tree)]


def test_detail_and_one_reexport_pass(tree):
    text = f'{DETAIL}class Rate {{}};\n}}\n{EXPORT}using detail::frame_rate::Rate;\n}}\n'
    assert findings(tree, text) == []


def test_driver_tree_is_sdl3(tree):
    text = 'namespace sdl3::rdp::detail::window {\nclass Window {};\n}\n'
    assert findings(tree, text, 'sources/sdl-rdp/SDL3/rdp/window.so.cpp') == []
    assert findings(tree, 'namespace sdl_rdp::sdl3::rdp::detail::window {\n}\n',
                    'sources/sdl-rdp/SDL3/rdp/window.so.cpp') == ['window.so.cpp:1']


def test_anonymous_namespace_inside_a_source_files_detail_passes(tree):
    assert findings(tree, f'{DETAIL}namespace {{\nint x;\n}}\n}}\n', SOURCE) == []


def test_anonymous_namespace_in_a_header_fails_anywhere(tree):
    assert findings(tree, f'{DETAIL}namespace {{\nint y;\n}}\n}}\n') == ['frame-rate.hpp:2']
    assert findings(tree, 'namespace {\nint x;\n}\n') == ['frame-rate.hpp:1', 'frame-rate.hpp:1']


def test_global_helpers_in_a_source_file_fail(tree):
    assert findings(tree, 'static int helper() { return 1; }\nnamespace {\nint y;\n}\n', SOURCE) == [
        'frame-rate.cpp:1', 'frame-rate.cpp:2']


def test_header_declaring_without_a_namespace_fails(tree):
    assert findings(tree, 'class Rate {\npublic:\n  auto F() -> void;\n};\n') == [
        'frame-rate.hpp:1', 'frame-rate.hpp:1']


def test_include_only_headers_pass(tree):
    assert findings(tree, '#pragma once\nextern "C" {\n#include <x.h>\n}\n#include <y.h>\n') == []


def test_runtime_hooks_and_sdl_tags_stay_global(tree):
    text = ('struct SDL_VideoData {\n  auto Bind() -> void;\n};\nauto SDL_VideoData::Bind() -> void { }\n'
            'extern "C" auto __libc_free(void* block) -> void;\nauto operator new(std::size_t size) -> void*;\n'
            f'{DETAIL}using sdl_rdp::link::PeerLink;\n}}\n')
    assert findings(tree, text, SOURCE) == []


def test_c_linkage_tables_live_in_the_detail_namespace(tree):
    table = 'extern "C" VideoBootStrap const RDP_bootstrap = { };\n'
    assert findings(tree, f'{DETAIL}{table}}}\n', SOURCE) == []
    assert findings(tree, table, SOURCE) == ['frame-rate.cpp:1']


BLOCK = 'extern "C" {\nstruct Helper {\n  int x;\n};\nauto helper() -> int { return 0; }\n}\n'


def test_extern_c_block_in_a_source_file_is_held_to_the_global_test(tree):
    assert findings(tree, BLOCK, SOURCE) == ['frame-rate.cpp:2', 'frame-rate.cpp:5']


def test_extern_c_block_in_a_header_is_held_to_the_global_test(tree):
    text = f'{DETAIL}class Rate {{}};\n}}\n{BLOCK}{EXPORT}using detail::frame_rate::Rate;\n}}\n'
    assert findings(tree, text) == ['frame-rate.hpp:5', 'frame-rate.hpp:8']


def test_extern_c_declaration_is_held_to_the_global_test(tree):
    assert findings(tree, 'extern "C" auto helper() -> int { return 0; }\n', SOURCE) == ['frame-rate.cpp:1']


def test_extern_variable_is_held_to_the_global_test(tree):
    assert findings(tree, 'extern int helper_counter;\n', SOURCE) == ['frame-rate.cpp:1']


def test_global_type_alias_in_a_source_file_fails(tree):
    assert findings(tree, 'using Alias = int;\n', SOURCE) == ['frame-rate.cpp:1']


def test_sdl_prefix_passes_only_for_sdl_tags(tree):
    text = 'auto SDL_MyHelper() -> int { return 1; }\nstruct SDL_Mine {\n  int x;\n};\n'
    assert findings(tree, text, SOURCE) == ['frame-rate.cpp:1', 'frame-rate.cpp:2']


def test_source_files_import_at_file_scope_and_headers_do_not(tree):
    text = f'using sdl_rdp::link::PeerLink;\n{DETAIL}}}\n'
    assert findings(tree, text, SOURCE) == []
    assert findings(tree, text) == ['frame-rate.hpp:1']


def test_inline_attributed_and_alias_namespaces_fail(tree):
    for text in ('inline namespace Foo {\n}\n', 'namespace [[deprecated]] Foo {\n}\n',
                 'namespace sdl_rdp::inline Foo {\n}\n'):
        assert findings(tree, f'{text}{DETAIL}}}\n') == ['frame-rate.hpp:1']
    assert findings(tree, f'{DETAIL}namespace fs = std::filesystem;\n}}\n') == ['frame-rate.hpp:2']


def test_foreign_and_upper_camel_names_fail(tree):
    assert findings(tree, 'namespace Backend {\n}\nnamespace sdl_rdp::video {\n}\n') == [
        'frame-rate.hpp:1', 'frame-rate.hpp:3']


def test_spaced_qualifiers_read_as_the_path(tree):
    text = 'namespace sdl_rdp :: video_codec :: detail :: frame_rate {\n}\nnamespace Backend :: Avc {\n}\n'
    assert findings(tree, text) == ['frame-rate.hpp:3']


def test_other_stem_detail_fails(tree):
    assert findings(tree, 'namespace sdl_rdp::video_codec::detail::other {\nint x;\n}\n') == ['frame-rate.hpp:1']


def test_nested_named_namespace_fails(tree):
    assert findings(tree, f'{DETAIL}namespace inner {{\n}}\n}}\n') == ['frame-rate.hpp:2']


def test_reexport_of_an_imported_name_fails(tree):
    text = (f'{DETAIL}using sdl_rdp::link::PeerLink;\nclass Rate {{}};\n}}\n'
            f'{EXPORT}using detail::frame_rate::PeerLink;\n}}\n')
    assert findings(tree, text) == ['frame-rate.hpp:5']


def test_reexport_of_a_member_or_missing_name_fails(tree):
    for name in ('member', 'Missing'):
        text = f'{DETAIL}class Rate {{\n  int member;\n}};\n}}\n{EXPORT}using detail::frame_rate::{name};\n}}\n'
        assert findings(tree, text) == ['frame-rate.hpp:6']


def test_reexport_reads_declarator_lists_and_constrained_templates(tree):
    text = (f'{DETAIL}inline constexpr int A = 1, B = 2;\ntemplate <auto... R>\n  requires(sizeof...(R) > 0)\n'
            f'class Releases {{}};\n}}\n{EXPORT}using detail::frame_rate::A;\nusing detail::frame_rate::B;\n'
            'using detail::frame_rate::Releases;\n}\n')
    assert findings(tree, text) == []


def test_export_block_holds_only_using_declarations(tree):
    text = f'{DETAIL}class Rate {{}};\n}}\n{EXPORT}using detail::frame_rate::Rate;\nint y;\n}}\n'
    assert findings(tree, text) == ['frame-rate.hpp:4']


def test_export_block_is_last(tree):
    text = (f'{DETAIL}class Rate {{}};\n}}\n{EXPORT}using detail::frame_rate::Rate;\n}}\n'
            f'{DETAIL}class Late {{}};\n}}\n')
    assert findings(tree, text) == ['frame-rate.hpp:7']
    once = f'{EXPORT}using detail::frame_rate::A;\n}}\n'
    text = f'{DETAIL}class A {{}};\n}}\n{once}{once}'
    assert findings(tree, text) == ['frame-rate.hpp:7']


def test_source_file_exports_nothing(tree):
    assert findings(tree, f'{EXPORT}using detail::frame_rate::Rate;\n}}\n', SOURCE) == ['frame-rate.cpp:1']


def test_using_namespace_fails_in_headers_only(tree):
    text = f'{DETAIL}auto F() -> void {{\n  using namespace std::chrono_literals;\n}}\n}}\n'
    assert findings(tree, text) == ['frame-rate.hpp:3']
    assert findings(tree, text.replace('frame-rate', 'clock'), SOURCE) == ['frame-rate.hpp:3']


def test_forward_lists_live_in_the_owning_folders_forward_header(tree):
    text = 'namespace sdl_rdp::video_codec::detail::clock {\nclass Clock;\n}\n'
    export = f'{EXPORT}using detail::clock::Clock;\n}}\n'
    assert findings(tree, text + export, 'sources/sdl-rdp/video-codec/forward.hpp') == []
    assert findings(tree, text) == ['frame-rate.hpp:1']


def test_forward_list_of_another_folder_or_a_missing_file_fails(tree):
    for text in ('namespace sdl_rdp::link::detail::peer_link {\nclass PeerLink;\n}\n',
                 'namespace sdl_rdp::video_codec::detail::missing {\nclass Missing;\n}\n',
                 'namespace sdl_rdp::video_codec::detail::clock {\nclass Clock {};\n}\n'):
        assert findings(tree, text, 'sources/sdl-rdp/video-codec/forward.hpp') == ['forward.hpp:1']


def test_detail_reach_outside_the_module_fails(tree):
    reach = 'using sdl_rdp::link::detail::peer_link::PeerLink;\n'
    assert findings(tree, f'{DETAIL}{reach}}}\n') == ['frame-rate.hpp:2']
    assert findings(tree, f'{DETAIL}using sdl_rdp::video_codec::detail::clock::Clock;\n}}\n') == []
    inner = ('namespace sdl_rdp::video_codec::gfx::detail::channel {\n'
             'using sdl_rdp::video_codec::detail::clock::Clock;\n}\n')
    (tree / 'sources/sdl-rdp/video-codec/gfx').mkdir()
    assert findings(tree, inner, 'sources/sdl-rdp/video-codec/gfx/channel.cpp') == []


def test_detail_reach_at_a_call_site_fails(tree):
    text = f'{DETAIL}auto F() -> void {{\n  sdl_rdp::link::detail::peer_link::PeerLink link;\n}}\n}}\n'
    assert findings(tree, text, SOURCE) == ['frame-rate.cpp:3']


def test_detail_reach_in_a_type_alias_fails(tree):
    text = f'{DETAIL}using Link = sdl_rdp::link::detail::peer_link::PeerLink;\n}}\n'
    assert findings(tree, text, SOURCE) == ['frame-rate.cpp:2']


def test_detail_reach_from_the_global_namespace_fails(tree):
    text = f'{DETAIL}using ::sdl_rdp::link::detail::peer_link::PeerLink;\n}}\n'
    assert findings(tree, text, SOURCE) == ['frame-rate.cpp:2']
    assert findings(tree, f'{DETAIL}using ::sdl_rdp::video_codec::detail::clock::Clock;\n}}\n', SOURCE) == []


def test_detail_reach_to_a_member_fails(tree):
    text = f'{DETAIL}using sdl_rdp::link::detail::peer_link::PeerLink::Kind;\n}}\n'
    assert findings(tree, text, SOURCE) == ['frame-rate.cpp:2']


def test_detail_reach_across_lines_fails(tree):
    text = f'{DETAIL}using sdl_rdp::link::detail::peer_link::\n    PeerLink;\n}}\n'
    assert findings(tree, text, SOURCE) == ['frame-rate.cpp:2']


def test_detail_directive_in_a_detail_block_fails(tree):
    text = f'{DETAIL}using namespace sdl_rdp::link::detail::peer_link;\n}}\n'
    assert findings(tree, text, SOURCE) == ['frame-rate.cpp:2']


def test_detail_directive_in_a_function_fails(tree):
    text = f'{DETAIL}auto F() -> void {{\n  using namespace sdl_rdp::link::detail::peer_link;\n}}\n}}\n'
    assert findings(tree, text, SOURCE) == ['frame-rate.cpp:3']


def test_source_without_its_header_defines_an_included_siblings_class(tree):
    member = 'sources/sdl-rdp/video-codec/loop.cpp'
    clock  = 'namespace sdl_rdp::video_codec::detail::clock {\nauto Clock::Tick() -> void { }\n}\n'
    assert findings(tree, f'#include <sdl-rdp/video-codec/clock.hpp>\n{clock}', member) == []
    assert findings(tree, f'#include <sdl-rdp/link/peer-link.hpp>\n{clock}', member) == ['loop.cpp:2']
    both = f'#include <sdl-rdp/video-codec/clock.hpp>\n{clock}namespace sdl_rdp::video_codec::detail::loop {{\n}}\n'
    assert findings(tree, both, member) == ['loop.cpp:5']


def test_source_with_its_header_opens_only_its_own_detail(tree):
    (tree / HEADER).write_text(f'{DETAIL}}}\n')
    text = '#include <sdl-rdp/video-codec/clock.hpp>\nnamespace sdl_rdp::video_codec::detail::clock {\n}\n'
    assert findings(tree, text, SOURCE) == ['frame-rate.cpp:2']
