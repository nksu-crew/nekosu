package me.nekosu.aqnya.ui.screens

import android.os.Build
import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Rule
import androidx.compose.material.icons.filled.Android
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.Numbers
import androidx.compose.material.icons.filled.PhoneAndroid
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import me.nekosu.aqnya.R
import me.nekosu.aqnya.ncore
import me.nekosu.aqnya.ui.component.StatusCard
import me.nekosu.aqnya.util.DebugPreferences
import me.nekosu.aqnya.util.getAppVersion

enum class InstallStatus {
    INSTALLED,
    NEED_UPDATE,
    NEED_REBOOT,
    NOT_INSTALLED,
    UNKNOWN,
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HomeScreenContent(
    installStatus: InstallStatus,
    isGki: Boolean,
    suCount: Int,
    ruleCount: Int,
    showRules: Boolean,
    managerVersion: String,
    onNavigateToApps: () -> Unit,
    onNavigateToRules: () -> Unit,
    onInstallClick: () -> Unit,
    onAboutClick: () -> Unit = {},
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = stringResource(R.string.app_name),
                    )
                },
                colors =
                    TopAppBarDefaults.topAppBarColors(
                        containerColor = MaterialTheme.colorScheme.surface,
                        titleContentColor = MaterialTheme.colorScheme.onSurface,
                    ),
            )
        },
    ) { innerPadding ->
        Column(
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(innerPadding)
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = 16.dp, vertical = 12.dp)
                    .padding(bottom = 88.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            StatusCard(
                state = installStatus,
                isGki = isGki,
                onCardClick = onInstallClick,
                workingText = stringResource(R.string.running),
                versionText = "$managerVersion - ${if (isGki) "GKI" else "LKM"}",
                customBadgeText = if (isGki) "GKI" else "LKM",
            )

            if (installStatus == InstallStatus.INSTALLED || showRules) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    StatCard(
                        label = stringResource(R.string.superuser),
                        value = suCount.toString(),
                        modifier = Modifier.weight(1f),
                        bgIcon = Icons.Filled.Numbers,
                        onClick = onNavigateToApps,
                    )
                    StatCard(
                        label = stringResource(R.string.fmac_rules),
                        value = ruleCount.toString(),
                        modifier = Modifier.weight(1f),
                        bgIcon = Icons.AutoMirrored.Filled.Rule,
                        onClick = onNavigateToRules,
                    )
                }
            }
            DeviceInfoCard(modifier = Modifier.fillMaxWidth())

            AboutCard(modifier = Modifier.fillMaxWidth(), onClick = onAboutClick)
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HomeScreen(
    viewModel: HomeViewModel,
    onNavigateToApps: () -> Unit = {},
    onNavigateToRules: () -> Unit = {},
    onAboutClick: () -> Unit = {},
) {
    val context = LocalContext.current
    var showInstallSheet by remember { mutableStateOf(false) }
    val showRules by DebugPreferences.showRulesFlow(context).collectAsState(initial = false)

    val installStatus by viewModel.installStatus.collectAsState()
    val suCount by viewModel.suCount.collectAsState()
    val ruleCount by viewModel.ruleCount.collectAsState()
    val managerVersion by viewModel.managerVersion.collectAsState()

    LaunchedEffect(Unit) {
        viewModel.installStatus.collect { status ->
            if (status == InstallStatus.INSTALLED) {
                viewModel.refresh()
                return@collect
            }
        }
    }

    HomeScreenContent(
        installStatus = installStatus,
        isGki = false, // TODO: detect is gki or lkm install.
        suCount = suCount,
        ruleCount = ruleCount,
        showRules = showRules,
        managerVersion = managerVersion,
        onNavigateToApps = onNavigateToApps,
        onNavigateToRules = onNavigateToRules,
        onInstallClick = {
            if (installStatus != InstallStatus.INSTALLED) {
                Toast.makeText(context, "即将上线... From Aqnya", Toast.LENGTH_SHORT).show()
                showInstallSheet = true
                ncore.ctl(1)
            } else {
                Toast.makeText(context, context.getString(R.string.running), Toast.LENGTH_SHORT).show()
            }
        },
        onAboutClick = onAboutClick,
    )

    LaunchedEffect(showInstallSheet) {
        if (showInstallSheet) {
            ncore.helloLog()
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun StatCard(
    label: String,
    value: String,
    modifier: Modifier = Modifier,
    bgIcon: ImageVector? = null,
    onClick: () -> Unit = {},
) {
    Card(
        modifier = modifier,
        onClick = onClick,
        shape = RoundedCornerShape(24.dp),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
        colors =
            CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceContainer,
            ),
    ) {
        Box(modifier = Modifier.fillMaxWidth()) {
            if (bgIcon != null) {
                Icon(
                    imageVector = bgIcon,
                    contentDescription = null,
                    modifier =
                        Modifier
                            .align(Alignment.BottomEnd)
                            .size(58.dp)
                            .padding(end = 18.dp, bottom = 12.dp),
                    tint = MaterialTheme.colorScheme.primary.copy(alpha = 0.35f),
                )
            }
            Column(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .padding(20.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                Text(
                    text = value,
                    style = MaterialTheme.typography.headlineMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onSurface,
                )
                Text(
                    text = label,
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.60f),
                )
            }
        }
    }
}

@Composable
fun DeviceInfoCard(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val appVersion = remember { getAppVersion(context) }

    val items =
        listOf(
            Triple(Icons.Filled.Memory, stringResource(id = R.string.kernel_version), System.getProperty("os.version") ?: "Unavailable"),
            Triple(Icons.Filled.Android, stringResource(id = R.string.android_version), Build.VERSION.RELEASE),
            Triple(Icons.Filled.PhoneAndroid, stringResource(id = R.string.device_model), "${Build.MANUFACTURER} ${Build.MODEL}"),
            Triple(Icons.Filled.Settings, stringResource(id = R.string.manager_version), appVersion),
            Triple(Icons.Outlined.Fingerprint,stringResource(id = R.string.finger_print),Build.FINGERPRINT),
        )

    Box(modifier = modifier.clip(RoundedCornerShape(28.dp))) {
        Box(
            modifier =
                Modifier
                    .matchParentSize()
                    .background(
                        brush =
                            Brush.verticalGradient(
                                colors =
                                    listOf(
                                        MaterialTheme.colorScheme.primary.copy(alpha = 0.08f),
                                        Color.Transparent,
                                    ),
                            ),
                    ).blur(24.dp),
        )

        Card(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(28.dp),
            elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
            colors =
                CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surfaceContainer,
                ),
        ) {
                items.forEach { (icon, title, value) ->
                    DeviceInfoItem(icon = icon, title = title, value = value)
                }
        }
    }
}

@Composable
fun DeviceInfoItem(
    icon: ImageVector,
    title: String,
    value: String,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier =
            modifier
                .fillMaxWidth()
                .padding(horizontal = 20.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            modifier = Modifier.size(20.dp),
        )

        Spacer(modifier = Modifier.width(16.dp))

        Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(
                text = title,
                style = MaterialTheme.typography.bodyLarge
            )
            Text(
                text = value,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.outline,
            )
        }
    }
}

@Composable
fun AboutCard(
    modifier: Modifier = Modifier,
    onClick: () -> Unit = {},
) {
    val context = LocalContext.current

    Card(
        modifier = modifier,
        onClick = onClick,
        shape = RoundedCornerShape(28.dp),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
        colors =
            CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceContainer,
            ),
    ) {
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .padding(20.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Spacer(modifier = Modifier.width(16.dp))

            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                Text(
                    text = stringResource(R.string.about_card_title),
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onSurface,
                )
                Text(
                    text = stringResource(R.string.about_card_description),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.60f),
                    maxLines = 2,
                )
            }
        }
    }
}

@Preview(showBackground = true, name = "Installed")
@Composable
fun HomeScreenPreviewInstalled() {
    MaterialTheme {
        HomeScreenContent(
            installStatus = InstallStatus.INSTALLED,
            isGki = true,
            suCount = 0,
            ruleCount = 0,
            showRules = true,
            managerVersion = "1.0.0",
            onNavigateToApps = {},
            onNavigateToRules = {},
            onInstallClick = {},
            onAboutClick = {},
        )
    }
}

@Preview(showBackground = true, name = "Not Install")
@Composable
fun HomeScreenPreviewNotInstalled() {
    MaterialTheme {
        HomeScreenContent(
            installStatus = InstallStatus.NOT_INSTALLED,
            isGki = false,
            suCount = 0,
            ruleCount = 0,
            showRules = true,
            managerVersion = "1.0.0",
            onNavigateToApps = {},
            onNavigateToRules = {},
            onInstallClick = {},
            onAboutClick = {},
        )
    }
}
