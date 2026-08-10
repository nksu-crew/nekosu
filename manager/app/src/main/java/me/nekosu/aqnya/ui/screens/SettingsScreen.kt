package me.nekosu.aqnya.ui.screens

import android.app.Activity
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
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
import me.nekosu.aqnya.util.TinkerManager
import me.nekosu.aqnya.util.TinkerUpdateChecker
import java.io.File

/** 热更新流程状态 */
private sealed class HotUpdateState {
    data object Idle : HotUpdateState()
    data object Checking : HotUpdateState()
    data object NoUpdate : HotUpdateState()
    data class UpdateAvailable(val info: TinkerUpdateChecker.PatchInfo) : HotUpdateState()
    data object Downloading : HotUpdateState()
    data class PatchReady(val patchFile: File) : HotUpdateState()
    data class Error(val message: String) : HotUpdateState()
}

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

    // 热更新状态
    var updateState by remember { mutableStateOf<HotUpdateState>(HotUpdateState.Idle) }

    // ── 热更新对话框 ──
    when (val state = updateState) {
        is HotUpdateState.Checking -> {
            AlertDialog(
                onDismissRequest = { },
                title = { Text(stringResource(R.string.hot_update_title)) },
                text = {
                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalAlignment = Alignment.CenterHorizontally,
                    ) {
                        CircularProgressIndicator()
                        Spacer(Modifier.padding(8.dp))
                        Text(stringResource(R.string.hot_update_checking))
                    }
                },
                confirmButton = { },
                dismissButton = {
                    TextButton(onClick = { updateState = HotUpdateState.Idle }) {
                        Text(stringResource(R.string.dialog_cancel))
                    }
                },
            )
        }

        is HotUpdateState.NoUpdate -> {
            AlertDialog(
                onDismissRequest = { updateState = HotUpdateState.Idle },
                title = { Text(stringResource(R.string.hot_update_title)) },
                text = { Text(stringResource(R.string.hot_update_no_update)) },
                confirmButton = {
                    TextButton(onClick = { updateState = HotUpdateState.Idle }) {
                        Text(stringResource(R.string.dialog_confirm))
                    }
                },
            )
        }

        is HotUpdateState.UpdateAvailable -> {
            AlertDialog(
                onDismissRequest = { updateState = HotUpdateState.Idle },
                title = { Text(stringResource(R.string.hot_update_title)) },
                text = {
                    Text(
                        stringResource(
                            R.string.hot_update_available,
                            state.info.releaseName,
                        ),
                    )
                },
                confirmButton = {
                    Button(onClick = {
                        updateState = HotUpdateState.Downloading
                        scope.launch {
                            val tmpFile =
                                TinkerUpdateChecker.downloadPatch(mContext, state.info)
                            if (tmpFile != null) {
                                val target = TinkerManager.moveToPatchDir(
                                    mContext, tmpFile, state.info.patchName,
                                )
                                if (target != null) {
                                    val result = TinkerManager.loadPatch(mContext, target)
                                    if (result is TinkerManager.LoadResult.Success) {
                                        updateState = HotUpdateState.PatchReady(target)
                                    } else {
                                        updateState = HotUpdateState.Error(
                                            (result as TinkerManager.LoadResult.Error).message,
                                        )
                                    }
                                } else {
                                    updateState = HotUpdateState.Error(
                                        mContext.getString(R.string.hot_update_error_move),
                                    )
                                }
                            } else {
                                updateState = HotUpdateState.Error(
                                    mContext.getString(R.string.hot_update_error_download),
                                )
                            }
                        }
                    }) {
                        Text(stringResource(R.string.hot_update_download_apply))
                    }
                },
                dismissButton = {
                    TextButton(onClick = { updateState = HotUpdateState.Idle }) {
                        Text(stringResource(R.string.dialog_cancel))
                    }
                },
            )
        }

        is HotUpdateState.Downloading -> {
            AlertDialog(
                onDismissRequest = { },
                title = { Text(stringResource(R.string.hot_update_title)) },
                text = {
                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalAlignment = Alignment.CenterHorizontally,
                    ) {
                        CircularProgressIndicator()
                        Spacer(Modifier.padding(8.dp))
                        Text(stringResource(R.string.hot_update_downloading))
                    }
                },
                confirmButton = { },
            )
        }

        is HotUpdateState.PatchReady -> {
            AlertDialog(
                onDismissRequest = { updateState = HotUpdateState.Idle },
                title = { Text(stringResource(R.string.hot_update_title)) },
                text = { Text(stringResource(R.string.hot_update_ready)) },
                confirmButton = {
                    Button(onClick = {
                        updateState = HotUpdateState.Idle
                        // 杀死进程以应用补丁
                        android.os.Process.killProcess(android.os.Process.myPid())
                    }) {
                        Text(stringResource(R.string.hot_update_restart))
                    }
                },
                dismissButton = {
                    TextButton(onClick = { updateState = HotUpdateState.Idle }) {
                        Text(stringResource(R.string.hot_update_later))
                    }
                },
            )
        }

        is HotUpdateState.Error -> {
            AlertDialog(
                onDismissRequest = { updateState = HotUpdateState.Idle },
                title = { Text(stringResource(R.string.hot_update_title)) },
                text = { Text(state.message) },
                confirmButton = {
                    TextButton(onClick = { updateState = HotUpdateState.Idle }) {
                        Text(stringResource(R.string.dialog_confirm))
                    }
                },
            )
        }

        HotUpdateState.Idle -> { /* 不显示对话框 */ }
    }

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
                onCheckUpdate = {
                    updateState = HotUpdateState.Checking
                    scope.launch {
                        // TODO: 替换为实际的 GitHub 仓库 owner/repo
                        val patch = TinkerUpdateChecker.fetchLatestPatch(
                            owner = "nksu-crew",
                            repo = "nekosu",
                        )
                        updateState = if (patch != null) {
                            HotUpdateState.UpdateAvailable(patch)
                        } else {
                            HotUpdateState.NoUpdate
                        }
                    }
                },
            )

            // ── 关于 ──
            AboutSection(
                onAboutClick = { navController.navigate("about") },
            )

            Spacer(Modifier)
        }
    }
}
