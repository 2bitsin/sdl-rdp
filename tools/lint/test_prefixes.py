"""The prefix lint: no file repeats the name of the folder holding it."""
import pytest

import prefixes


@pytest.fixture
def tree(tmp_path):
    for name, text in {'sources/pkg/clipboard/CMakeLists.txt': '',
                       'sources/pkg/client.test/CMakeLists.txt': '',
                       'sources/pkg/session/CMakeLists.txt': '',
                       'sources/pkg/session/session.hpp': 'class Session final : public Access {\n};\n',
                       'sources/pkg/session/session.cpp': '',
                       'sources/pkg/client.test/client.test.cpp': '',
                       'sources/pkg/clipboard/store.hpp': ''}.items():
        (tmp_path / name).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / name).write_text(text)
    return tmp_path


def names(tree, name, text=''):
    (tree / name).parent.mkdir(parents=True, exist_ok=True)
    (tree / name).write_text(text)
    return [str(path) for path in prefixes.findings(tree)]


def test_plain_names_and_the_class_namesake_pass(tree):
    assert prefixes.findings(tree) == []


def test_directory_prefix_fails(tree):
    assert names(tree, 'sources/pkg/clipboard/clipboard-text.cpp') == ['sources/pkg/clipboard/clipboard-text.cpp']


def test_test_directory_prefix_is_its_stem(tree):
    assert names(tree, 'sources/pkg/client.test/client-steps.hpp') == ['sources/pkg/client.test/client-steps.hpp']


def test_namesake_of_another_class_fails(tree):
    assert names(tree, 'sources/pkg/clipboard/clipboard.hpp', 'class Store;\nclass ClipboardChannel {\n};\n') == [
        'sources/pkg/clipboard/clipboard.hpp']


def test_subfolder_prefix_fails(tree):
    found = names(tree, 'sources/pkg/clipboard/drive/drive-channel.hpp')
    assert found == ['sources/pkg/clipboard/drive/drive-channel.hpp']


def test_sdl_driver_prefix_fails(tree):
    assert names(tree, 'sources/pkg/session/rdp/SDL_rdpvideo.cpp') == ['sources/pkg/session/rdp/SDL_rdpvideo.cpp']


def test_namesake_in_a_plain_subfolder_fails(tree):
    assert names(tree, 'sources/pkg/session/rdp/audio/audio.cpp', 'auto AudioRate() -> void;\n') == [
        'sources/pkg/session/rdp/audio/audio.cpp']


def test_private_folder_answers_for_its_parent(tree):
    assert names(tree, 'sources/pkg/clipboard/_detail/clipboard-dispatch.hpp') == [
        'sources/pkg/clipboard/_detail/clipboard-dispatch.hpp']


def test_bench_partition_namesake_fails(tree):
    assert names(tree, 'sources/pkg/session/audio.bench/audio.cpp') == ['sources/pkg/session/audio.bench/audio.cpp']
