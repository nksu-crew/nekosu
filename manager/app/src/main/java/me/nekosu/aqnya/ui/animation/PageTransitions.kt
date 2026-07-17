package me.nekosu.aqnya.ui.animation

import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.slideOutVertically

/**
 * 页面路由过渡动画
 */
object PageTransitions {
    val defaultEnter = fadeIn(tween(300))
    val defaultExit = fadeOut(tween(300))

    /**
     * 创建页面过渡动画
     * @param speed 动画速度倍率 (默认1.0)
     * @param animationType 动画类型: "linear", "spatial", "fade", "vertical", "diagonal"
     * @param direction 方向: 1=正向, -1=反向
     * @param isNavigationRail 是否使用侧边导航栏（影响滑动轴）
     * @return Pair(enterTransition, exitTransition)
     */
    fun createPageTransitions(
        speed: Float = 1.0f,
        animationType: String = "linear",
        direction: Int = 1,
        isNavigationRail: Boolean = false,
    ): Pair<EnterTransition, ExitTransition> {
        val duration = (300f / speed).toInt().coerceIn(100, 800)

        return when (animationType) {
            "spatial" -> {
                val enter =
                    fadeIn(tween(duration)) +
                        scaleIn(
                            initialScale = if (direction > 0) 0.9f else 1.1f,
                            animationSpec = tween(duration),
                        )
                val exit =
                    fadeOut(tween(duration)) +
                        scaleOut(
                            targetScale = if (direction > 0) 1.1f else 0.9f,
                            animationSpec = tween(duration),
                        )
                Pair(enter, exit)
            }
            "fade" -> {
                Pair(fadeIn(tween(duration)), fadeOut(tween(duration)))
            }
            "vertical" -> {
                val offsetY = if (direction > 0) { h: Int -> h } else { h: Int -> -h }
                val enter =
                    slideInVertically(
                        animationSpec = tween(duration),
                        initialOffsetY = offsetY,
                    ) + fadeIn(tween(duration))
                val exit =
                    slideOutVertically(
                        animationSpec = tween(duration),
                        targetOffsetY = if (direction > 0) { h: Int -> -h } else { h: Int -> h },
                    ) + fadeOut(tween(duration))
                Pair(enter, exit)
            }
            "diagonal" -> {
                val offsetX = if (direction > 0) { w: Int -> w } else { w: Int -> -w }
                val offsetY = if (direction > 0) { h: Int -> h } else { h: Int -> -h }
                val enter =
                    slideInHorizontally(
                        animationSpec = tween(duration),
                        initialOffsetX = offsetX,
                    ) +
                        slideInVertically(
                            animationSpec = tween(duration),
                            initialOffsetY = offsetY,
                        ) + fadeIn(tween(duration))
                val exit =
                    slideOutHorizontally(
                        animationSpec = tween(duration),
                        targetOffsetX = if (direction > 0) { w: Int -> -w } else { w: Int -> w },
                    ) +
                        slideOutVertically(
                            animationSpec = tween(duration),
                            targetOffsetY = if (direction > 0) { h: Int -> -h } else { h: Int -> h },
                        ) + fadeOut(tween(duration))
                Pair(enter, exit)
            }
            else -> { // linear
                if (isNavigationRail) {
                    val offsetY = if (direction > 0) { h: Int -> h } else { h: Int -> -h }
                    val enter =
                        slideInVertically(
                            animationSpec = tween(duration),
                            initialOffsetY = offsetY,
                        ) + fadeIn(tween(duration))
                    val exit =
                        slideOutVertically(
                            animationSpec = tween(duration),
                            targetOffsetY = if (direction > 0) { h: Int -> -h } else { h: Int -> h },
                        ) + fadeOut(tween(duration))
                    Pair(enter, exit)
                } else {
                    val offsetX = if (direction > 0) { w: Int -> w } else { w: Int -> -w }
                    val enter =
                        slideInHorizontally(
                            animationSpec = tween(duration),
                            initialOffsetX = offsetX,
                        ) + fadeIn(tween(duration))
                    val exit =
                        slideOutHorizontally(
                            animationSpec = tween(duration),
                            targetOffsetX = if (direction > 0) { w: Int -> -w } else { w: Int -> w },
                        ) + fadeOut(tween(duration))
                    Pair(enter, exit)
                }
            }
        }
    }
}
