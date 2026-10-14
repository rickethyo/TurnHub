package com.turnhub.android.data

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioManager
import android.media.SoundPool
import com.turnhub.android.R
import com.turnhub.android.domain.GameSoundCue

/** Short, original cues on the media stream; no background service or audio permission. */
class GameSoundPlayer(context: Context) {
    private val audio = context.applicationContext.getSystemService(AudioManager::class.java)
    private val loaded = java.util.concurrent.ConcurrentHashMap.newKeySet<Int>()
    private val pool = SoundPool.Builder().setMaxStreams(1).setAudioAttributes(
        AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build(),
    ).build().apply {
        setOnLoadCompleteListener { _, sound, status -> if (status == 0) loaded.add(sound) }
    }
    private val sounds = mapOf(
        GameSoundCue.START to R.raw.game_start,
        GameSoundCue.PASS to R.raw.game_pass,
        GameSoundCue.PAUSE to R.raw.game_pause,
        GameSoundCue.RESUME to R.raw.game_resume,
        GameSoundCue.WIN to R.raw.game_win,
        GameSoundCue.DRAW to R.raw.game_draw,
    ).mapValues { (_, resource) -> pool.load(context.applicationContext, resource, 1) }
    private var stream = 0
    private var foreground = false

    fun setForeground(on: Boolean) { foreground = on; if (!on) stop() }

    fun play(cue: GameSoundCue, volume: Int) {
        val manager = audio ?: return
        if (!foreground || volume <= 0 || manager.ringerMode != AudioManager.RINGER_MODE_NORMAL ||
            manager.getStreamVolume(AudioManager.STREAM_MUSIC) == 0) return
        val sound = sounds[cue]?.takeIf { it in loaded } ?: return
        stop()
        val level = volume.coerceIn(0, 100) / 100f
        stream = pool.play(sound, level, level, 1, 0, 1f)
    }

    fun stop() { if (stream != 0) pool.stop(stream); stream = 0 }
    fun release() { stop(); pool.release() }
}
