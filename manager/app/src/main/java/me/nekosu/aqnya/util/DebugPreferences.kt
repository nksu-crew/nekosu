package me.nekosu.aqnya.util

import android.content.Context
import android.content.SharedPreferences
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow

object DebugPreferences {
    private const val PREF_NAME = "app_preferences"

    private fun getPrefs(context: Context): SharedPreferences = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

    fun showRulesFlow(context: Context): Flow<Boolean> = context.prefsFlow { getBoolean("debug_show_rules", false) }

    fun themeModeFlow(context: Context): Flow<Int> = context.prefsFlow { getInt("theme_mode", 0) }

    fun navBarStyleFlow(context: Context): Flow<Int> = context.prefsFlow { getInt("nav_bar_style", 0) }

    fun themeColorFlow(context: Context): Flow<Int> = context.prefsFlow { getInt("theme_color", 0) }

    fun amoledFlow(context: Context): Flow<Boolean> = context.prefsFlow { getBoolean("amoled_mode", false) }

    fun animationTypeFlow(context: Context): Flow<String> = context.prefsFlow { getString("folkx_animation_type", "linear") ?: "linear" }

    suspend fun setAnimationType(
        context: Context,
        type: String,
    ) {
        getPrefs(context).edit().putString("folkx_animation_type", type).apply()
    }

    fun animationSpeedFlow(context: Context): Flow<Float> = context.prefsFlow { getFloat("folkx_animation_speed", 1.0f) }

    suspend fun setAnimationSpeed(
        context: Context,
        speed: Float,
    ) {
        getPrefs(context).edit().putFloat("folkx_animation_speed", speed.coerceIn(0.5f, 2.0f)).apply()
    }

    fun setShowRules(
        context: Context,
        value: Boolean,
    ) {
        getPrefs(context).edit().putBoolean("debug_show_rules", value).apply()
    }

    fun setThemeMode(
        context: Context,
        mode: Int,
    ) {
        getPrefs(context).edit().putInt("theme_mode", mode).apply()
    }

    fun setNavBarStyle(
        context: Context,
        value: Int,
    ) {
        getPrefs(context).edit().putInt("nav_bar_style", value).apply()
    }

    fun setThemeColor(
        context: Context,
        value: Int,
    ) {
        getPrefs(context).edit().putInt("theme_color", value).apply()
    }

    fun setAmoled(
        context: Context,
        value: Boolean,
    ) {
        getPrefs(context).edit().putBoolean("amoled_mode", value).apply()
    }

    fun pagerAnimationStyleFlow(context: Context): Flow<Int> = context.prefsFlow { getInt("pager_anim_style", 0) }

    fun setPagerAnimationStyle(
        context: Context,
        value: Int,
    ) {
        getPrefs(context).edit().putInt("pager_anim_style", value).apply()
    }

    private fun <T> Context.prefsFlow(getValue: SharedPreferences.() -> T): Flow<T> =
        callbackFlow {
            val prefs = getPrefs(this@prefsFlow)
            val listener =
                SharedPreferences.OnSharedPreferenceChangeListener { p, key ->
                    trySend(p.getValue())
                }

            prefs.registerOnSharedPreferenceChangeListener(listener)
            trySend(prefs.getValue())

            awaitClose {
                prefs.unregisterOnSharedPreferenceChangeListener(listener)
            }
        }
}
