package me.nekosu.aqnya.util

import androidx.annotation.StringRes
import me.nekosu.aqnya.R

enum class NavBarStyle(
    @param:StringRes val titleRes: Int,
    val value: Int,
) {
    FLOATING(R.string.navbar_style_floating, 0),
    NORMAL(R.string.navbar_style_normal, 1),
    ;

    companion object {
        fun fromValue(value: Int) = entries.find { it.value == value } ?: FLOATING
    }
}
