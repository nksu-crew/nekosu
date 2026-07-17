package me.nekosu.aqnya.ui.component

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.CheckCircle
import androidx.compose.material.icons.outlined.SystemUpdate
import androidx.compose.material.icons.outlined.Warning
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import me.nekosu.aqnya.R
import me.nekosu.aqnya.ui.screens.InstallStatus

/**
 * 状态卡片 - 显示安装状态
 *
 * @param state InstallStatus 状态
 * @param isGki 是否为 GKI 内核（true=GKI，false=LKM）
 * @param onCardClick 卡片点击回调
 * @param workingText 工作状态显示文本
 * @param versionText 版本显示文本
 * @param customBadgeText 自定义徽章文本（可选）
 * @param containerColor 自定义容器颜色（可选）
 */
@Composable
fun StatusCard(
    state: InstallStatus,
    isGki: Boolean,
    onCardClick: () -> Unit,
    workingText: String,
    versionText: String,
    customBadgeText: String? = null,
    containerColor: Color? = null,
) {
    val isWorking = state == InstallStatus.INSTALLED
    val isUpdate = state == InstallStatus.NEED_UPDATE || state == InstallStatus.NEED_REBOOT

    val finalContainerColor =
        containerColor ?: if (isWorking) {
            MaterialTheme.colorScheme.secondaryContainer
        } else {
            MaterialTheme.colorScheme.errorContainer
        }

    StatusCardContainer(containerColor = finalContainerColor) {
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .clickable { onCardClick() }
                    .padding(24.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (isWorking) {
                WorkingStatusContent(
                    isGki = isGki,
                    workingText = workingText,
                    versionText = versionText,
                    customBadgeText = customBadgeText,
                )
            } else {
                NonWorkingStatusContent(
                    isUpdate = isUpdate,
                    needUpdateText = stringResource(R.string.status_card_need_update),
                    notInstalledText = stringResource(R.string.status_card_not_installed),
                    clickToInstallText = stringResource(R.string.status_card_click_to_install),
                )
            }
        }
    }
}

@Composable
private fun WorkingStatusContent(
    isGki: Boolean,
    workingText: String,
    versionText: String,
    customBadgeText: String?,
) {
    Icon(Icons.Outlined.CheckCircle, workingText)

    Column(Modifier.padding(start = 20.dp)) {
        val modeText = customBadgeText ?: if (isGki) "GKI" else "LKM"

        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = workingText,
                style = MaterialTheme.typography.titleMedium,
            )
            Spacer(Modifier.width(8.dp))

            ModeLabel(label = modeText)
        }

        Spacer(Modifier.height(4.dp))

        Text(
            text = versionText,
            style = MaterialTheme.typography.bodyMedium,
        )
    }
}

@Composable
private fun NonWorkingStatusContent(
    isUpdate: Boolean,
    needUpdateText: String,
    notInstalledText: String,
    clickToInstallText: String,
) {
    val icon = if (isUpdate) Icons.Outlined.SystemUpdate else Icons.Outlined.Warning
    val title = if (isUpdate) needUpdateText else notInstalledText

    Icon(icon, title)

    Column(Modifier.padding(start = 20.dp)) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleMedium,
        )
        Spacer(Modifier.height(4.dp))
        Text(
            text = clickToInstallText,
            style = MaterialTheme.typography.bodyMedium,
        )
    }
}

@Composable
private fun StatusCardContainer(
    containerColor: Color,
    content: @Composable () -> Unit,
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = containerColor),
        shape = RoundedCornerShape(20.dp),
    ) {
        content()
    }
}

@Composable
fun ModeLabel(
    label: String,
    modifier: Modifier = Modifier,
    color: Color = MaterialTheme.colorScheme.onPrimary,
    containerColor: Color = MaterialTheme.colorScheme.primary,
) {
    Box(
        modifier =
            modifier
                .padding(end = 4.dp)
                .background(
                    color = containerColor,
                    shape = RoundedCornerShape(4.dp),
                ),
    ) {
        Text(
            text = label,
            modifier = Modifier.padding(vertical = 2.dp, horizontal = 5.dp),
            style =
                TextStyle(
                    fontSize = 10.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = color,
                ),
        )
    }
}
