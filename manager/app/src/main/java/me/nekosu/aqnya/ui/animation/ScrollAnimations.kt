package me.nekosu.aqnya.ui.animation

import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource

object ScrollAnimations {
    const val DEFAULT_SCROLL_THRESHOLD = 8f

    @Composable
    fun rememberNavBarScrollConnection(
        onVisibilityChange: (Boolean) -> Unit,
        scrollThreshold: Float = DEFAULT_SCROLL_THRESHOLD,
        animationSpeed: Float = 1.0f,
    ): NestedScrollConnection {
        return remember(animationSpeed) {
            object : NestedScrollConnection {
                private val effectiveThreshold = scrollThreshold / animationSpeed.coerceAtLeast(0.5f)

                override fun onPreScroll(
                    available: Offset,
                    source: NestedScrollSource,
                ): Offset {
                    if (available.y < -effectiveThreshold) {
                        onVisibilityChange(false)
                    }
                    if (available.y > effectiveThreshold) {
                        onVisibilityChange(true)
                    }
                    return Offset.Zero
                }
            }
        }
    }
}
