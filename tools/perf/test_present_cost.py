"""present_cost's profile reading and per-present summary, offline against a saved profile."""
import pathlib

import pytest

import present_cost

FIXTURE = pathlib.Path(__file__).parent / 'fixtures' / 'two-presents.callgrind'
PRESENT = ('Backend::Presenter::Present(std::span<unsigned char const, 18446744073709551615ul>, unsigned int, '
           'Backend::Extent, std::span<sdlrdp_rect const, 18446744073709551615ul>)')


@pytest.fixture(scope='module')
def profile():
    return present_cost.parse_profile(FIXTURE)


def test_self_cost_excludes_call_lines(profile):
    assert profile.self_cost[PRESENT] == 100030
    assert profile.self_cost['Backend::FrameSnapshot::Pixels() const'] == 22


def test_compressed_names_resolve_on_reuse(profile):
    assert profile.calls[('rdp::(anonymous namespace)::UpdateFramebuffer(SDL_VideoDevice*, SDL_Window*, '
                          'SDL_Rect const*, int)', 'sdlrdp_present')] == 2
    assert profile.calls[('operator new(unsigned long)', 'malloc')] == 2


def test_call_edges_carry_count_and_inclusive_cost(profile):
    assert profile.calls[('sdlrdp_present', PRESENT)] == 2
    assert profile.inclusive[('sdlrdp_present', PRESENT)] == 140000


def test_summary_is_per_present(profile):
    assert present_cost.summarize(profile) == {
        'presents':                   2,
        'poll memset Ir/present':     39000.0,
        'compose Ir/present':         50026.0,
        'driver allocations/present': 1.0,
    }


def test_backend_allocations_are_not_the_driver(profile):
    assert profile.calls[('Stream_New', 'malloc')] == 6
    assert present_cost.summarize(profile)['driver allocations/present'] == 1.0


def test_empty_profile_summarizes_to_zero(tmp_path):
    empty = tmp_path / 'empty.callgrind'
    empty.write_text('# callgrind format\nevents: Ir\n')
    assert present_cost.summarize(present_cost.parse_profile(empty))['presents'] == 0
