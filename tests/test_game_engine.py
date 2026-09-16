"""Behavioral tests for TurnHub's hardware-independent game engine.

These tests intentionally describe current behavior. They are the safety net
for the upcoming architecture work, not a redesign of the rules themselves.
"""

from __future__ import annotations

from unittest.mock import patch

import pytest

from config import STATE_GAME_OVER, STATE_PAUSED, STATE_RUNNING, WARNING_OFF
from game_engine import GameEngine
from player import PlayerSeat


def players(count: int = 4) -> list[PlayerSeat]:
    return [PlayerSeat(number, number - 1, 1) for number in range(1, count + 1)]


def shared_players() -> list[PlayerSeat]:
    return [
        PlayerSeat(1, 0, 1),
        PlayerSeat(2, 0, 2),
        PlayerSeat(3, 1, 1),
        PlayerSeat(4, 2, 1),
    ]


def started_game(count: int = 4, *, profile: str = "generic", life: int = 40) -> GameEngine:
    seats = players(count)
    game = GameEngine()
    game.start(seats, seats[0], WARNING_OFF, starting_life=life, game_profile=profile)
    return game


def test_start_initializes_game_and_player_state() -> None:
    game = started_game(4, profile="mtg_commander", life=40)

    assert game.state == STATE_RUNNING
    assert game.active_player_number == 1
    assert game.starter_player == 1
    assert game.game_profile == "mtg_commander"
    assert game.life_totals == {1: 40, 2: 40, 3: 40, 4: 40}
    assert set(game.stats) == {1, 2, 3, 4}
    assert game.eliminated_players == []
    assert game.commander_damage[1] == {2: 0, 3: 0, 4: 0}


def test_start_can_choose_non_first_starter() -> None:
    seats = players(4)
    game = GameEngine()
    game.start(seats, seats[2], WARNING_OFF)

    assert game.active_player_number == 3
    assert game.starter_player == 3
    assert game.active_module == 2


def test_pass_turn_advances_and_records_completed_turn() -> None:
    game = started_game(3)
    game.turn_started_at = 100.0

    with patch("game_engine.time.monotonic", return_value=112.5):
        assert game.pass_turn(0, 300_000)

    assert game.active_player_number == 2
    assert game.stats[1].turns_completed == 1
    assert game.stats[1].total_turn_seconds == pytest.approx(12.5)
    assert game.current_warning_ms == 300_000


def test_wrong_module_cannot_pass_active_turn() -> None:
    game = started_game(3)

    assert not game.pass_turn(1, WARNING_OFF)
    assert game.active_player_number == 1
    assert game.stats[1].turns_completed == 0


def test_pause_and_resume_do_not_charge_pause_to_turn() -> None:
    game = started_game(2)
    game.turn_started_at = 100.0

    with patch("game_engine.time.monotonic", return_value=110.0):
        assert game.pause(armed_by=0)

    assert game.state == STATE_PAUSED
    assert game.current_turn_elapsed(150.0) == pytest.approx(10.0)

    with patch("game_engine.time.monotonic", return_value=140.0):
        assert game.resume()

    assert game.state == STATE_RUNNING
    assert game.total_paused_seconds == pytest.approx(30.0)
    assert game.turn_started_at == pytest.approx(130.0)
    assert game.current_turn_elapsed(145.0) == pytest.approx(15.0)


def test_life_adjustment_is_independent_from_elimination() -> None:
    game = started_game(2, life=20)

    assert game.adjust_life(1, -20)
    assert game.life_total(1) == 0
    assert not game.is_eliminated(1)
    assert game.active_player_number == 1


def test_cross_player_life_change_can_be_denied_without_erasing_later_changes() -> None:
    game = started_game(3)

    with patch("game_engine.time.time", return_value=1_000.0):
        notice = game.adjust_life_from_web(1, 2, -5)

    assert notice is not None
    assert game.life_total(2) == 35

    assert game.adjust_life(2, 2)
    assert game.life_total(2) == 37

    with patch("game_engine.time.time", return_value=1_001.0):
        assert game.deny_life_change(2, notice["notice_id"])

    assert game.life_total(2) == 42


def test_self_life_change_does_not_create_pending_notice() -> None:
    game = started_game(2)

    result = game.adjust_life_from_web(1, 1, -1)

    assert result is not None
    assert game.life_total(1) == 39
    assert game.pending_life_changes == []


def test_commander_damage_only_available_for_commander_profile() -> None:
    normal = started_game(2, profile="mtg")
    commander = started_game(2, profile="mtg_commander")

    assert not normal.adjust_commander_damage(1, 2, 5)
    assert commander.adjust_commander_damage(1, 2, 5)
    assert commander.commander_damage_total(1, 2) == 5
    assert not commander.adjust_commander_damage(1, 1, 5)
    assert not commander.adjust_commander_damage(1, 2, -6)


def test_eliminating_non_active_player_removes_them_from_future_turn_order() -> None:
    game = started_game(4)
    assert game.pause()

    eliminated, finished = game.eliminate_player(3, WARNING_OFF)

    assert eliminated
    assert not finished
    assert game.is_eliminated(3)

    assert game.resume()
    assert game.pass_turn(0, WARNING_OFF)
    assert game.active_player_number == 2
    assert game.pass_turn(1, WARNING_OFF)
    assert game.active_player_number == 4


def test_eliminating_active_player_moves_to_next_living_player() -> None:
    game = started_game(3)
    assert game.pause()

    eliminated, finished = game.eliminate_player(1, 300_000)

    assert eliminated
    assert not finished
    assert game.active_player_number == 2
    assert game.current_warning_ms == 300_000


def test_last_living_player_automatically_wins() -> None:
    game = started_game(2)
    assert game.pause()

    eliminated, finished = game.eliminate_player(1, WARNING_OFF)

    assert eliminated
    assert finished
    assert game.state == STATE_GAME_OVER
    assert game.winner_player == 2


def test_win_claim_requires_other_living_players_to_confirm() -> None:
    game = started_game(3)

    assert game.begin_win_claim(1)
    assert game.state == STATE_PAUSED
    assert game.win_claim_player == 1
    assert game.win_claim_required == [2, 3]

    accepted, finished = game.confirm_win_claim(2)
    assert accepted and not finished

    accepted, finished = game.confirm_win_claim(3)
    assert accepted and finished
    assert game.state == STATE_GAME_OVER
    assert game.winner_player == 1


def test_win_claim_denial_restores_running_game() -> None:
    game = started_game(3)

    assert game.begin_win_claim(1)
    assert game.state == STATE_PAUSED
    assert game.deny_win_claim(2)

    assert not game.has_win_claim
    assert game.state == STATE_RUNNING
    assert game.winner_player is None


def test_shared_module_win_confirmation_order_follows_modules() -> None:
    seats = shared_players()
    game = GameEngine()
    game.start(seats, seats[0], WARNING_OFF)

    assert game.begin_win_claim(1)

    # Module 1, then Module 2, then the claimant's other logical seat.
    assert game.win_claim_required == [3, 4, 2]


def test_pass_turn_on_shared_module_advances_between_logical_players() -> None:
    seats = shared_players()
    game = GameEngine()
    game.start(seats, seats[0], WARNING_OFF)

    assert game.pass_turn(0, WARNING_OFF)
    assert game.active_player_number == 2
    assert game.active_module == 0

    assert game.pass_turn(0, WARNING_OFF)
    assert game.active_player_number == 3
    assert game.active_module == 1
