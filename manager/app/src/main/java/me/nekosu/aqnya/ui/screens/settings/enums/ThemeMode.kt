package me.nekosu.aqnya.ui.screens.enums

import androidx.annotation.StringRes
import me.nekosu.aqnya.R

enum class ThemeMode(
    @param:StringRes val titleRes: Int,
    val value: Int,
) {
    SYSTEM(R.string.theme_system, 0),
    LIGHT(R.string.theme_light, 1),
    DARK(R.string.theme_dark, 2),
    ;

    companion object {
        fun fromValue(value: Int) = entries.find { it.value == value } ?: SYSTEM
    }
}
