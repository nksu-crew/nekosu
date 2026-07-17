package me.nekosu.aqnya.ui.theme

import android.os.Build
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import me.nekosu.aqnya.util.DebugPreferences

// ============================================================
// 三个独立配色方案 — 不再交换色值，各自硬编码
// ============================================================

private fun staticDark(v: Int) =
    when (v) {
        2 -> {
            darkColorScheme( // 紫
                primary = SecondaryDark,
                onPrimary = OnSecondaryDark,
                primaryContainer = SecondaryContainerDark,
                onPrimaryContainer = OnSecondaryContainerDark,
                secondary = PrimaryDark,
                onSecondary = OnPrimaryDark,
                secondaryContainer = PrimaryContainerDark,
                onSecondaryContainer = OnPrimaryContainerDark,
                tertiary = TertiaryDark,
                onTertiary = OnTertiaryDark,
                tertiaryContainer = TertiaryContainerDark,
                onTertiaryContainer = OnTertiaryContainerDark,
                background = Color(0xFF1C1A26),
                surface = Color(0xFF1C1A26),
                surfaceVariant = Color(0xFF2C2836),
                surfaceContainerLowest = Color(0xFF1C1A26),
                surfaceContainerLow = Color(0xFF1C1A26),
                surfaceContainer = Color(0xFF2C2836),
                surfaceContainerHigh = Color(0xFF2C2836),
                surfaceContainerHighest = Color(0xFF2C2836),
            )
        }

        3 -> {
            darkColorScheme( // 绿
                primary = TertiaryDark,
                onPrimary = OnTertiaryDark,
                primaryContainer = TertiaryContainerDark,
                onPrimaryContainer = OnTertiaryContainerDark,
                secondary = SecondaryDark,
                onSecondary = OnSecondaryDark,
                secondaryContainer = SecondaryContainerDark,
                onSecondaryContainer = OnSecondaryContainerDark,
                tertiary = PrimaryDark,
                onTertiary = OnPrimaryDark,
                tertiaryContainer = PrimaryContainerDark,
                onTertiaryContainer = OnPrimaryContainerDark,
                background = Color(0xFF18201E),
                surface = Color(0xFF18201E),
                surfaceVariant = Color(0xFF242E2A),
                surfaceContainerLowest = Color(0xFF18201E),
                surfaceContainerLow = Color(0xFF18201E),
                surfaceContainer = Color(0xFF242E2A),
                surfaceContainerHigh = Color(0xFF242E2A),
                surfaceContainerHighest = Color(0xFF242E2A),
            )
        }

        else -> {
            darkColorScheme( // 蓝
                primary = PrimaryDark,
                onPrimary = OnPrimaryDark,
                primaryContainer = PrimaryContainerDark,
                onPrimaryContainer = OnPrimaryContainerDark,
                secondary = SecondaryDark,
                onSecondary = OnSecondaryDark,
                secondaryContainer = SecondaryContainerDark,
                onSecondaryContainer = OnSecondaryContainerDark,
                tertiary = TertiaryDark,
                onTertiary = OnTertiaryDark,
                tertiaryContainer = TertiaryContainerDark,
                onTertiaryContainer = OnTertiaryContainerDark,
                background = Color(0xFF181B24),
                surface = Color(0xFF181B24),
                surfaceVariant = Color(0xFF262B38),
                surfaceContainerLowest = Color(0xFF181B24),
                surfaceContainerLow = Color(0xFF181B24),
                surfaceContainer = Color(0xFF262B38),
                surfaceContainerHigh = Color(0xFF262B38),
                surfaceContainerHighest = Color(0xFF262B38),
            )
        }
    }

private fun staticLight(v: Int) =
    when (v) {
        2 -> {
            lightColorScheme( // 紫
                primary = SecondaryLight,
                onPrimary = OnSecondaryLight,
                primaryContainer = SecondaryContainerLight,
                onPrimaryContainer = OnSecondaryContainerLight,
                secondary = PrimaryLight,
                onSecondary = OnPrimaryLight,
                secondaryContainer = PrimaryContainerLight,
                onSecondaryContainer = OnPrimaryContainerLight,
                tertiary = TertiaryLight,
                onTertiary = OnTertiaryLight,
                tertiaryContainer = TertiaryContainerLight,
                onTertiaryContainer = OnTertiaryContainerLight,
                background = Color(0xFFF8F6FC),
                surface = Color(0xFFF8F6FC),
                surfaceVariant = Color(0xFFEDE6F4),
                surfaceContainerLowest = Color(0xFFF8F6FC),
                surfaceContainerLow = Color(0xFFF8F6FC),
                surfaceContainer = Color(0xFFEDE6F4),
                surfaceContainerHigh = Color(0xFFEDE6F4),
                surfaceContainerHighest = Color(0xFFEDE6F4),
            )
        }

        3 -> {
            lightColorScheme( // 绿
                primary = TertiaryLight,
                onPrimary = OnTertiaryLight,
                primaryContainer = TertiaryContainerLight,
                onPrimaryContainer = OnTertiaryContainerLight,
                secondary = SecondaryLight,
                onSecondary = OnSecondaryLight,
                secondaryContainer = SecondaryContainerLight,
                onSecondaryContainer = OnSecondaryContainerLight,
                tertiary = PrimaryLight,
                onTertiary = OnPrimaryLight,
                tertiaryContainer = PrimaryContainerLight,
                onTertiaryContainer = OnPrimaryContainerLight,
                background = Color(0xFFF6FAF8),
                surface = Color(0xFFF6FAF8),
                surfaceVariant = Color(0xFFE2EDE6),
                surfaceContainerLowest = Color(0xFFF6FAF8),
                surfaceContainerLow = Color(0xFFF6FAF8),
                surfaceContainer = Color(0xFFE2EDE6),
                surfaceContainerHigh = Color(0xFFE2EDE6),
                surfaceContainerHighest = Color(0xFFE2EDE6),
            )
        }

        else -> {
            lightColorScheme( // 蓝
                primary = PrimaryLight,
                onPrimary = OnPrimaryLight,
                primaryContainer = PrimaryContainerLight,
                onPrimaryContainer = OnPrimaryContainerLight,
                secondary = SecondaryLight,
                onSecondary = OnSecondaryLight,
                secondaryContainer = SecondaryContainerLight,
                onSecondaryContainer = OnSecondaryContainerLight,
                tertiary = TertiaryLight,
                onTertiary = OnTertiaryLight,
                tertiaryContainer = TertiaryContainerLight,
                onTertiaryContainer = OnTertiaryContainerLight,
                background = Color(0xFFF8FAFE),
                surface = Color(0xFFF8FAFE),
                surfaceVariant = Color(0xFFE4EAF4),
                surfaceContainerLowest = Color(0xFFF8FAFE),
                surfaceContainerLow = Color(0xFFF8FAFE),
                surfaceContainer = Color(0xFFE4EAF4),
                surfaceContainerHigh = Color(0xFFE4EAF4),
                surfaceContainerHighest = Color(0xFFE4EAF4),
            )
        }
    }

private fun amoledBlend(v: Int) = staticDark(v).copy(background = AmoledBlack, surface = AmoledBlack)

@Composable
fun NekosuTheme(content: @Composable () -> Unit) {
    val context = LocalContext.current
    val prefs = remember { context.getSharedPreferences("saya", android.content.Context.MODE_PRIVATE) }

    val themePreference by DebugPreferences.themeModeFlow(context).collectAsState(initial = prefs.getInt("theme_mode", 0))

    var themeColorValue by remember { mutableIntStateOf(prefs.getInt("theme_color", 0)) }
    DisposableEffect(prefs) {
        val listener =
            android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == "theme_color") themeColorValue = prefs.getInt("theme_color", 0)
            }
        prefs.registerOnSharedPreferenceChangeListener(listener)
        onDispose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
    }

    var amoledEnabled by remember { mutableStateOf(prefs.getBoolean("amoled_mode", false)) }
    DisposableEffect(prefs) {
        val listener =
            android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == "amoled_mode") amoledEnabled = prefs.getBoolean("amoled_mode", false)
            }
        prefs.registerOnSharedPreferenceChangeListener(listener)
        onDispose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
    }

    val useDynamicColor = themeColorValue == 0 && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
    val isDarkTheme =
        when (themePreference) {
            1 -> false
            2 -> true
            else -> isSystemInDarkTheme()
        }

    val colorScheme =
        when {
            amoledEnabled && isDarkTheme && useDynamicColor -> {
                dynamicDarkColorScheme(
                    context,
                ).copy(background = AmoledBlack, surface = AmoledBlack)
            }

            amoledEnabled && isDarkTheme -> {
                amoledBlend(themeColorValue)
            }

            useDynamicColor -> {
                if (isDarkTheme) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
            }

            isDarkTheme -> {
                staticDark(themeColorValue)
            }

            else -> {
                staticLight(themeColorValue)
            }
        }

    // Edge-to-Edge 系统栏样式
    EdgeToEdgeSystemBars(darkMode = isDarkTheme)

    MaterialTheme(
        colorScheme = colorScheme,
        typography = Typography,
        content = content,
    )
}

// ============================================================
// Edge-to-Edge 沉浸式系统栏
// ============================================================

@Composable
private fun EdgeToEdgeSystemBars(
    darkMode: Boolean,
    statusBarScrim: Color = Color.Transparent,
    navigationBarScrim: Color = Color.Transparent,
) {
    val context = LocalContext.current
    val activity = context as? ComponentActivity ?: return

    SideEffect {
        activity.enableEdgeToEdge(
            statusBarStyle =
                SystemBarStyle.auto(
                    statusBarScrim.toArgb(),
                    statusBarScrim.toArgb(),
                ) { darkMode },
            navigationBarStyle =
                when {
                    darkMode -> {
                        SystemBarStyle.dark(navigationBarScrim.toArgb())
                    }

                    else -> {
                        SystemBarStyle.light(
                            navigationBarScrim.toArgb(),
                            navigationBarScrim.toArgb(),
                        )
                    }
                },
        )
    }
}
