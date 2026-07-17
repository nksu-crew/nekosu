package me.nekosu.aqnya.ui.animation

import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.pager.PagerState
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import me.nekosu.aqnya.util.PagerAnimationStyle

object PagerAnimations {
    /**
     * 根据动画样式切换到指定页面
     *
     * @param pagerState Pager 状态
     * @param targetPage 目标页面索引
     * @param animationStyle 动画样式
     * @param scope 协程作用域
     * @param animationSpeed 动画速度倍率（影响弹簧刚度和时长）
     */
    fun navigateToPage(
        pagerState: PagerState,
        targetPage: Int,
        animationStyle: PagerAnimationStyle,
        scope: CoroutineScope,
        animationSpeed: Float = 1.0f,
    ) {
        scope.launch {
            when (animationStyle) {
                PagerAnimationStyle.SPRING -> {
                    val stiffness = (300f * animationSpeed * animationSpeed).coerceIn(50f, 1200f)
                    pagerState.animateScrollToPage(
                        targetPage,
                        animationSpec =
                            spring(
                                stiffness = stiffness,
                            ),
                    )
                }
                PagerAnimationStyle.DEFAULT -> {
                    val duration = (300f / animationSpeed).toInt().coerceIn(100, 800)
                    pagerState.animateScrollToPage(
                        targetPage,
                        animationSpec = tween(duration),
                    )
                }
            }
        }
    }
}
