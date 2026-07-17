package me.nekosu.aqnya.ui.screens.sections

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Info
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import me.nekosu.aqnya.R
import me.nekosu.aqnya.ui.component.CardGroup
import me.nekosu.aqnya.ui.component.CardItem

@Composable
fun AboutSection(onAboutClick: () -> Unit) {
    CardGroup {
        CardItem(index = 0, total = 1) {
            ListItem(
                modifier = Modifier.fillMaxWidth().clickable { onAboutClick() },
                colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                leadingContent = { Icon(Icons.Outlined.Info, contentDescription = null) },
                headlineContent = {
                    Text(
                        stringResource(R.string.about),
                    )
                },
            )
        }
    }
}
