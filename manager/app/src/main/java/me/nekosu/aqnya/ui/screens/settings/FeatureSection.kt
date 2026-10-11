package me.nekosu.aqnya.ui.screens.sections

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Security
import androidx.compose.material.icons.outlined.Tune
import androidx.compose.material3.Icon
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.nekosu.aqnya.R
import me.nekosu.aqnya.ui.component.CardGroup
import me.nekosu.aqnya.ui.component.CardItem
import me.nekosu.aqnya.ui.component.ListRow
import me.nekosu.aqnya.util.KernelFeature

/**
 * 内核 feature 开关：列表与状态都由内核给出（IOC_FEATURE_LIST/SET），管理器只
 * 负责展示、切换，并把开关同步到开机的标志文件。
 */
@Composable
fun FeatureSection() {
    val scope = rememberCoroutineScope()
    var features by remember { mutableStateOf<List<KernelFeature.Info>>(emptyList()) }
    var busy by remember { mutableStateOf(false) }

    suspend fun reload() {
        features = withContext(Dispatchers.IO) { KernelFeature.list() }
    }
    LaunchedEffect(Unit) { reload() }

    CardGroup {
        if (features.isEmpty()) {
            CardItem(index = 0, total = 1) {
                ListRow(
                    icon = { Icon(Icons.Outlined.Tune, contentDescription = null) },
                    headline = { Text(stringResource(R.string.feature_empty)) },
                    supporting = { Text(stringResource(R.string.feature_empty_describe)) },
                )
            }
        } else {
            features.forEachIndexed { index, feature ->
                CardItem(index = index, total = features.size) {
                    ListRow(
                        icon = { Icon(feature.icon(), contentDescription = null) },
                        headline = { Text(featureLabel(feature)) },
                        supporting = { Text(featureSummary(feature)) },
                        trailing = {
                            Switch(
                                checked = feature.enabled,
                                enabled = !busy,
                                onCheckedChange = { checked ->
                                    scope.launch {
                                        busy = true
                                        val ok =
                                            withContext(Dispatchers.IO) {
                                                KernelFeature.set(feature.id, checked)
                                            }
                                        busy = false
                                        if (ok) reload()
                                    }
                                },
                            )
                        },
                    )
                }
            }
        }
    }
}

private fun KernelFeature.Info.icon(): ImageVector =
    when (id) {
        KernelFeature.SELINUX_HIDE -> Icons.Outlined.Security
        else -> Icons.Outlined.Tune
    }

@Composable
private fun featureLabel(feature: KernelFeature.Info): String =
    when (feature.id) {
        KernelFeature.SELINUX_HIDE -> stringResource(R.string.feature_selinux_hide)
        // 未知 feature 直接显示内核给的名字。
        else -> feature.name
    }

@Composable
private fun featureSummary(feature: KernelFeature.Info): String =
    when (feature.id) {
        KernelFeature.SELINUX_HIDE -> stringResource(R.string.feature_selinux_hide_describe)
        else -> feature.name
    }
