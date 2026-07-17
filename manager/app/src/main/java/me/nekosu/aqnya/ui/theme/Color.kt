package me.nekosu.aqnya.ui.theme

import androidx.compose.ui.graphics.Color

// ============================================================
// Nekosu 配色方案 — Catppuccin 莫兰迪色系
// 柔和、低饱和、护眼，适用于深色和浅色主题
// ============================================================

// ── 浅色主题主色 ──
val PrimaryLight = Color(0xFF5B7EC2)
val OnPrimaryLight = Color(0xFFFFFFFF)
val PrimaryContainerLight = Color(0xFFD8E2FF)
val OnPrimaryContainerLight = Color(0xFF001A40)

val SecondaryLight = Color(0xFF8A6DA6)
val OnSecondaryLight = Color(0xFFFFFFFF)
val SecondaryContainerLight = Color(0xFFF1DBFF)
val OnSecondaryContainerLight = Color(0xFF2F174B)

val TertiaryLight = Color(0xFF3E8E6F)
val OnTertiaryLight = Color(0xFFFFFFFF)
val TertiaryContainerLight = Color(0xFFC0FAD7)
val OnTertiaryContainerLight = Color(0xFF002115)

// ── 深色主题主色 ──
val PrimaryDark = Color(0xFF8AADF4) // Catppuccin Blue
val OnPrimaryDark = Color(0xFF001A40)
val PrimaryContainerDark = Color(0xFF2A4A7E)
val OnPrimaryContainerDark = Color(0xFFD8E2FF)

val SecondaryDark = Color(0xFFCEB5E4) // Catppuccin Lavender
val OnSecondaryDark = Color(0xFF341758)
val SecondaryContainerDark = Color(0xFF5B4A73)
val OnSecondaryContainerDark = Color(0xFFF1DBFF)

val TertiaryDark = Color(0xFF8BD5B0) // Catppuccin Green
val OnTertiaryDark = Color(0xFF003825)
val TertiaryContainerDark = Color(0xFF1E5E47)
val OnTertiaryContainerDark = Color(0xFFC0FAD7)

// ── 浅色表面色 ──
val LightSurface = Color(0xFFFAF8FC)
val LightSurfaceVariant = Color(0xFFE9E4F0)

// ── AMOLED 与辅助色 ──
val AmoledBlack = Color(0xFF000000) // 纯黑 AMOLED
val DarkSurface = Color(0xFF1A1B22) // 深色表面
val DarkSurfaceVariant = Color(0xFF2A2B33) // 深色表面变体

// ── 向后兼容别名 ──
@Deprecated("Use PrimaryLight", ReplaceWith("PrimaryLight"))
val Purple40 = PrimaryLight

@Deprecated("Use SecondaryLight", ReplaceWith("SecondaryLight"))
val PurpleGrey40 = SecondaryLight

@Deprecated("Use TertiaryLight", ReplaceWith("TertiaryLight"))
val Pink40 = TertiaryLight

@Deprecated("Use PrimaryDark", ReplaceWith("PrimaryDark"))
val Purple80 = PrimaryDark

@Deprecated("Use SecondaryDark", ReplaceWith("SecondaryDark"))
val PurpleGrey80 = SecondaryDark

@Deprecated("Use TertiaryDark", ReplaceWith("TertiaryDark"))
val Pink80 = TertiaryDark

// ── 工具函数 ──
fun Color.blend(
    other: Color,
    ratio: Float,
): Color {
    val inverse = 1f - ratio
    return Color(
        red = red * inverse + other.red * ratio,
        green = green * inverse + other.green * ratio,
        blue = blue * inverse + other.blue * ratio,
        alpha = alpha * inverse + other.alpha * ratio,
    )
}
