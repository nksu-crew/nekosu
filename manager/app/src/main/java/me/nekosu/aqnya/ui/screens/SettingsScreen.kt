package me.nekosu.aqnya.ui.screens

import android.app.Activity
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import kotlinx.coroutines.launch
import me.nekosu.aqnya.R
import me.nekosu.aqnya.ui.screens.enums.ThemeColor
import me.nekosu.aqnya.ui.screens.enums.ThemeMode
import me.nekosu.aqnya.ui.screens.sections.*
import me.nekosu.aqnya.util.DebugPreferences
import me.nekosu.aqnya.util.LocaleHelper
import me.nekosu.aqnya.util.LogUtils
import me.nekosu.aqnya.util.NavBarStyle

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(navController: NavController) {
    val mContext = LocalContext.current
    val scope = rememberCoroutineScope()
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())

    // 现有设置
    val themeValue by DebugPreferences.themeModeFlow(mContext).collectAsState(initial = 0)
    val currentThemeMode = ThemeMode.fromValue(themeValue)

    val navBarStyleValue by DebugPreferences.navBarStyleFlow(mContext).collectAsState(initial = 0)
    val currentNavBarStyle = NavBarStyle.fromValue(navBarStyleValue)

    val themeColorValue by DebugPreferences.themeColorFlow(mContext).collectAsState(initial = 0)
    val currentThemeColor = ThemeColor.fromValue(themeColorValue)

    val amoledEnabled by DebugPreferences.amoledFlow(mContext).collectAsState(initial = false)

    val currentLang =
        LocaleHelper
            .savedLanguageTag(mContext)
            .ifBlank { "" }
    val currentLangLabel =
        LocaleHelper.availableLanguages
            .find { it.tag == currentLang }
            ?.let { stringResource(it.labelRes) }
            ?: stringResource(R.string.language_system)

    Scaffold(
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = stringResource(R.string.settings_title),
                    )
                },
                scrollBehavior = scrollBehavior,
                colors =
                    TopAppBarDefaults.topAppBarColors(
                        containerColor = MaterialTheme.colorScheme.surface,
                        scrolledContainerColor = MaterialTheme.colorScheme.surface,
                    ),
            )
        },
        contentWindowInsets =
            WindowInsets.safeDrawing.only(
                WindowInsetsSides.Top + WindowInsetsSides.Horizontal,
            ),
    ) { innerPadding ->
        Column(
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(innerPadding)
                    .verticalScroll(rememberScrollState())
                    .padding(top = 16.dp)
                    .padding(bottom = 96.dp)
                    .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            // ── 语言 ──
            LanguageSection(
                currentLangLabel = currentLangLabel,
                onLanguageChange = { tag ->
                    LocaleHelper.saveLanguageTag(mContext, tag)
                    (mContext as? Activity)?.let { LocaleHelper.applyLanguage(it) }
                },
            )

            // ── 外观 ──
            AppearanceSection(
                currentThemeMode = currentThemeMode,
                currentNavBarStyle = currentNavBarStyle,
                currentThemeColor = currentThemeColor,
                amoledEnabled = amoledEnabled,
                onThemeChange = { mode ->
                    scope.launch { DebugPreferences.setThemeMode(mContext, mode.value) }
                },
                onNavBarStyleChange = { style ->
                    scope.launch { DebugPreferences.setNavBarStyle(mContext, style.value) }
                },
                onThemeColorChange = { color ->
                    scope.launch { DebugPreferences.setThemeColor(mContext, color.value) }
                },
                onAmoledChange = { enabled ->
                    scope.launch { DebugPreferences.setAmoled(mContext, enabled) }
                },
            )

            // ── 工具 ──
            ToolsSection(
                onExportLog = { LogUtils.exportLogs(mContext) },
            )

            // ── 关于 ──
            AboutSection(
                onAboutClick = { navController.navigate("about") },
            )

            Spacer(Modifier)
        }
    }
}
