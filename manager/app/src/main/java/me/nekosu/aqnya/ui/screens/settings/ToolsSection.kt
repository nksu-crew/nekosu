package me.nekosu.aqnya.ui.screens.sections

import androidx.compose.foundation.clickable
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.BugReport
import androidx.compose.material.icons.outlined.ChevronRight
import androidx.compose.material.icons.outlined.Science
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import me.nekosu.aqnya.R
import me.nekosu.aqnya.ui.component.CardGroup
import me.nekosu.aqnya.ui.component.CardItem
import me.nekosu.aqnya.ui.component.ListRow

@Composable
fun ToolsSection(
    onExportLog: () -> Unit,
    onDebugClick: () -> Unit,
) {
    CardGroup {
        CardItem(index = 0, total = 2) {
            ListRow(
                modifier = Modifier.clickable { onExportLog() },
                icon = { Icon(Icons.Outlined.BugReport, contentDescription = null) },
                headline = {
                    Text(
                        stringResource(R.string.export_log),
                    )
                },
                supporting = { Text(stringResource(R.string.export_log_describe)) },
            )
        }

        CardItem(index = 1, total = 2) {
            ListRow(
                modifier = Modifier.clickable { onDebugClick() },
                icon = { Icon(Icons.Outlined.Science, contentDescription = null) },
                headline = {
                    Text(
                        stringResource(R.string.settings_debug),
                    )
                },
                supporting = { Text(stringResource(R.string.settings_debug_summary)) },
                trailing = {
                    Icon(Icons.Outlined.ChevronRight, null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
                },
            )
        }
    }
}
