package me.nekosu.aqnya.ui.screens.sections

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.outlined.Animation
import androidx.compose.material.icons.outlined.Speed
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import me.nekosu.aqnya.R
import me.nekosu.aqnya.ui.component.CardGroup
import me.nekosu.aqnya.ui.component.CardItem
import me.nekosu.aqnya.ui.component.ListRow
import me.nekosu.aqnya.ui.screens.enums.AnimationType

@Composable
fun AnimationEngineSection(
    currentAnimType: AnimationType,
    currentSpeed: Float,
    onAnimTypeChange: (AnimationType) -> Unit,
    onSpeedChange: (Float) -> Unit,
) {
    var typeMenuExpanded by remember { mutableStateOf(false) }

    CardGroup {
        // 动画类型
        CardItem(index = 0, total = 2) {
            ListItem(
                modifier = Modifier.fillMaxWidth().clickable { typeMenuExpanded = true },
                colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                leadingContent = { Icon(Icons.Outlined.Animation, contentDescription = null) },
                headlineContent = {
                    Text(stringResource(R.string.animation_type_title))
                },
                supportingContent = { Text(currentAnimType.label) },
                trailingContent = {
                    Box {
                        DropdownMenu(
                            expanded = typeMenuExpanded,
                            onDismissRequest = { typeMenuExpanded = false },
                            shape = RoundedCornerShape(16.dp),
                        ) {
                            AnimationType.entries.forEach { type ->
                                DropdownMenuItem(
                                    text = {
                                        Text(
                                            type.label,
                                            fontWeight =
                                                if (currentAnimType == type) {
                                                    FontWeight.SemiBold
                                                } else {
                                                    FontWeight.Normal
                                                },
                                        )
                                    },
                                    onClick = {
                                        typeMenuExpanded = false
                                        onAnimTypeChange(type)
                                    },
                                    trailingIcon = {
                                        if (currentAnimType == type) {
                                            Icon(Icons.Default.Check, null, Modifier.size(20.dp))
                                        }
                                    },
                                )
                            }
                        }
                    }
                },
            )
        }

        // 动画速度
        CardItem(index = 1, total = 2) {
            Column(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .padding(vertical = 4.dp),
            ) {
                ListRow(
                    icon = { Icon(Icons.Outlined.Speed, contentDescription = null) },
                    headline = {
                        Text(stringResource(R.string.animation_speed_title))
                    },
                    supporting = {
                        Text("${String.format("%.1f", currentSpeed)}x")
                    },
                )
                Slider(
                    value = currentSpeed,
                    onValueChange = onSpeedChange,
                    valueRange = 0.5f..2.0f,
                    steps = 15,
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 16.dp, vertical = 4.dp),
                )
            }
        }
    }
}
