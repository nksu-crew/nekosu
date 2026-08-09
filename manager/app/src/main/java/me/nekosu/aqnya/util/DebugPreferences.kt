package me.nekosu.aqnya.util

import android.content.Context
import android.content.SharedPreferences
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow

object DebugPreferences {
    const val PREF_NAME = "app_preferences"

    private fun getPrefs(context: Context): SharedPreferences = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

    fun showRulesFlow(context: Context): Flow<Boolean> = context.prefsFlow { getBoolean("debug_show_rules", false) }

    fun themeModeFlow(context: Context): Flow<Int> = context.prefsFlow { getInt("theme_mode", 0) }

    fun navBarStyleFlow(context: Context): Flow<Int> = context.prefsFlow { getInt("nav_bar_style", 0) }

    fun themeColorFlow(context: Context): Flow<Int> = context.prefsFlow { getInt("theme_color", 0) }

    fun amoledFlow(context: Context): Flow<Boolean> = context.prefsFlow { getBoolean("amoled_mode", false) }

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
