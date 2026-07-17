package me.nekosu.aqnya.util

import androidx.annotation.StringRes
import me.nekosu.aqnya.R

enum class PagerAnimationStyle(
    @param:StringRes val titleRes: Int,
    val value: Int,
) {
    DEFAULT(R.string.pager_anim_default, 0),
    SPRING(R.string.pager_anim_spring, 1),
    ;

    companion object {
        fun fromValue(value: Int) = entries.find { it.value == value } ?: DEFAULT
    }
}
