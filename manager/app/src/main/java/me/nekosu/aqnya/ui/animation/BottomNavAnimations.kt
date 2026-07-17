package me.nekosu.aqnya.ui.animation

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import me.nekosu.aqnya.util.BottomNavItem
import me.nekosu.aqnya.util.NavBarStyle

/**
 * 带动画的底部导航栏
 * @param animationSpeed 动画速度倍率（默认1.0）
 */
@Composable
fun AnimatedBottomNavBar(
    visible: Boolean,
    style: NavBarStyle,
    items: List<BottomNavItem>,
    selectedIndex: Int,
    onTabClick: (Int) -> Unit,
    modifier: Modifier = Modifier,
    animationSpeed: Float = 1.0f,
    content: @Composable (List<BottomNavItem>, Int, (Int) -> Unit, NavBarStyle) -> Unit,
) {
    when (style) {
        NavBarStyle.NORMAL -> {
            FadeSlideNavBar(
                visible = visible,
                modifier = modifier,
                animationSpeed = animationSpeed,
            ) {
                content(items, selectedIndex, onTabClick, style)
            }
        }
        NavBarStyle.FLOATING -> {
            SlideNavBar(
                visible = visible,
                modifier = modifier,
                animationSpeed = animationSpeed,
            ) {
                content(items, selectedIndex, onTabClick, style)
            }
        }
    }
}

@Composable
private fun FadeSlideNavBar(
    visible: Boolean,
    modifier: Modifier = Modifier,
    animationSpeed: Float = 1.0f,
    content: @Composable () -> Unit,
) {
    val durationIn = (200f / animationSpeed).toInt().coerceIn(50, 600)
    val durationOut = (150f / animationSpeed).toInt().coerceIn(50, 600)

    val animatedAlpha by animateFloatAsState(
        targetValue = if (visible) 1f else 0f,
        animationSpec =
            tween(
                durationMillis = if (visible) durationIn else durationOut,
            ),
        label = "fadeSlideNavBarAlpha",
    )

    Box(
        modifier =
            modifier.graphicsLayer {
                alpha = animatedAlpha
                translationY = (1f - animatedAlpha) * size.height
            },
    ) {
        content()
    }
}

@Composable
private fun SlideNavBar(
    visible: Boolean,
    modifier: Modifier = Modifier,
    animationSpeed: Float = 1.0f,
    content: @Composable () -> Unit,
) {
    val durationIn = (200f / animationSpeed).toInt().coerceIn(50, 600)
    val durationOut = (150f / animationSpeed).toInt().coerceIn(50, 600)

    AnimatedVisibility(
        visible = visible,
        enter = fadeIn(tween(durationIn)) + slideInVertically { it },
        exit = fadeOut(tween(durationOut)) + slideOutVertically { it },
        modifier = modifier,
    ) {
        content()
    }
}
