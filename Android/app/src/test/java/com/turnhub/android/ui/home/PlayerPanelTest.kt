package com.turnhub.android.ui.home

import com.turnhub.android.data.PlayerSessionState
import com.turnhub.android.domain.TableSummaryMapper
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.GameSettingsInfo
import com.turnhub.android.protocol.SessionInfo
import com.turnhub.android.protocol.TableSettings
import com.turnhub.android.testing.Fixtures
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PlayerPanelTest {

    private fun summary(name: String = "running.response.json", edit: JSONObject.() -> Unit = {}) =
        TableSummaryMapper.map(Fixtures.info(), Fixtures.state(name, edit))

    private fun signedIn(player: Int, participating: Boolean = true, host: Boolean = false) =
        PlayerSessionState.SignedIn(
            profileId = "p1",
            name = "Ricky",
            info = SessionInfo("p1", "Ricky", 8, 1, player, participating, host, false, false),
        )

    private val settings = GameSettingsInfo(
        settings = TableSettings(GameProfile.GENERIC, 40, 90_000),
        available = true,
        canEdit = true,
        turnTimerPresetsMs = listOf(0, 60_000, 120_000, 180_000, 300_000),
        turnTimerMinMs = 15_000,
        turnTimerMaxMs = 3_600_000,
    )

    @Test
    fun `signed out offers only sign-in`() {
        val panel = PlayerPanel.from(summary(), PlayerSessionState.SignedOut, busy = false, feedback = null)
        assertFalse(panel.signedIn)
        assertFalse(panel.canPass || panel.canJoin || panel.canPauseResume)
    }

    @Test
    fun `signed in but not seated can join only in the lobby`() {
        val lobby = PlayerPanel.from(summary("lobby.response.json"), signedIn(0, participating = false), false, null)
        assertTrue(lobby.canJoin && lobby.status.contains("Join the table"))

        val running = PlayerPanel.from(summary(), signedIn(0, participating = false), false, null)
        assertFalse(running.canJoin)
        assertTrue(running.status.contains("in progress"))
    }

    @Test
    fun `the active player can pass and pause, others can only pause`() {
        val mine = PlayerPanel.from(summary(), signedIn(1), false, null)
        assertTrue(mine.canPass && mine.canPauseResume)
        assertEquals("Pause", mine.pauseResumeLabel)
        assertTrue(mine.status.contains("your turn"))

        val theirs = PlayerPanel.from(summary(), signedIn(2), false, null)
        assertFalse(theirs.canPass)
        assertTrue(theirs.canPauseResume)
    }

    @Test
    fun `a pending pass becomes cancel pass`() {
        val pending = summary { getJSONObject("pending").put("passPlayer", 1).put("passGraceRemainingMs", 2000) }
        assertEquals("Cancel pass", PlayerPanel.from(pending, signedIn(1), false, null).passLabel)
    }

    @Test
    fun `paused offers resume unless a table decision is pending`() {
        val paused = PlayerPanel.from(summary { put("state", "PAUSED") }, signedIn(2), false, null)
        assertEquals("Resume", paused.pauseResumeLabel)
        assertTrue(paused.canPauseResume)
        assertFalse(paused.canPass)

        val claim = summary {
            put("state", "PAUSED")
            getJSONObject("pending").put("winClaimPlayer", 1).put("winConfirmationPlayer", 2)
        }
        assertFalse(PlayerPanel.from(claim, signedIn(2), false, null).canPauseResume)
    }

    @Test
    fun `eliminated players and busy phones cannot act`() {
        val out = summary {
            put("state", "RUNNING")
            getJSONArray("players").getJSONObject(1).put("eliminated", true)
        }
        assertFalse(PlayerPanel.from(out, signedIn(2), false, null).canPauseResume)
        val busy = PlayerPanel.from(summary(), signedIn(1), busy = true, feedback = null)
        assertFalse(busy.canPass || busy.canPauseResume)
    }

    @Test
    fun `the host edits the timer only in the lobby`() {
        val lobby = summary("lobby.response.json") {
            put("players", org.json.JSONArray().put(
                JSONObject("""{"playerNumber":1,"moduleId":8,"slot":1,"participantId":1,"turnsCompleted":0,
                    "eliminated":false,"life":null,"commanderDamage":[],"lifeRequest":null}"""),
            ))
        }
        val editor = PlayerPanel.from(lobby, signedIn(1, host = true), false, null, settings).timerEditor!!
        assertEquals(90_000L, editor.currentMs)
        assertFalse(editor.currentIsPreset)
        assertNull(PlayerPanel.from(summary(), signedIn(1, host = true), false, null, settings).timerEditor)
        assertNull(PlayerPanel.from(lobby, signedIn(1), false, null, settings.copy(canEdit = false)).timerEditor)
    }

    @Test
    fun `custom timer input is checked against Atlas's range`() {
        val editor = TurnTimerEditor(0, listOf(0, 60_000), 15_000, 3_600_000)
        assertEquals(90_000L, editor.customMs("90"))
        assertEquals(15_000L, editor.customMs(" 15 "))
        assertNull(editor.customMs("14"))
        assertNull(editor.customMs("3601"))
        assertNull(editor.customMs(""))
        assertNull(editor.customMs("1.5"))
    }
}
