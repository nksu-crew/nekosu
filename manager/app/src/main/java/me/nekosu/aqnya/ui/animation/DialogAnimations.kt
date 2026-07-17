package me.nekosu.aqnya.ui.animation

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.material3.AlertDialog
import androidx.compose.runtime.Composable

/**
 * 对话框动画配置
 */
object DialogAnimations {
    /**
     * 创建对话框进入动画
     * @param speed 速度倍率 (0.5x ~ 2.0x)
     */
    fun createEnterAnimation(speed: Float = 1.0f) =
        fadeIn(tween((250f / speed).toInt().coerceIn(50, 600))) +
            scaleIn(
                initialScale = 0.8f,
                animationSpec = tween((250f / speed).toInt().coerceIn(50, 600)),
            )

    /**
     * 创建对话框退出动画
     * @param speed 速度倍率 (0.5x ~ 2.0x)
     */
    fun createExitAnimation(speed: Float = 1.0f) =
        fadeOut(tween((200f / speed).toInt().coerceIn(50, 600))) +
            scaleOut(
                targetScale = 0.8f,
                animationSpec = tween((200f / speed).toInt().coerceIn(50, 600)),
            )

    // 默认配置（速度1.0），方便直接引用
    val enter = createEnterAnimation()
    val exit = createExitAnimation()
}

/**
 * 带动画的 AlertDialog
 * @param animationSpeed 动画速度倍率
 */
@Composable
fun AnimatedAlertDialog(
    visible: Boolean,
    onDismiss: () -> Unit,
    title: @Composable () -> Unit,
    text: @Composable () -> Unit,
    confirmButton: @Composable () -> Unit,
    dismissButton: @Composable (() -> Unit)? = null,
    animationSpeed: Float = 1.0f,
) {
    AnimatedVisibility(
        visible = visible,
        enter = DialogAnimations.createEnterAnimation(animationSpeed),
        exit = DialogAnimations.createExitAnimation(animationSpeed),
    ) {
        AlertDialog(
            onDismissRequest = onDismiss,
            title = title,
            text = text,
            confirmButton = confirmButton,
            dismissButton = dismissButton?.let { btn -> { btn() } },
        )
    }
}
