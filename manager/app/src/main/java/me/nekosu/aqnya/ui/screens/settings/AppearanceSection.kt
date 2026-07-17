package me.nekosu.aqnya.ui.screens.sections

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ViewQuilt
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.outlined.DarkMode
import androidx.compose.material.icons.outlined.Palette
import androidx.compose.material.icons.outlined.PhoneAndroid
import androidx.compose.material.icons.outlined.Science
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import me.nekosu.aqnya.R
import me.nekosu.aqnya.ui.component.CardGroup
import me.nekosu.aqnya.ui.component.CardItem
import me.nekosu.aqnya.ui.component.ListRow
import me.nekosu.aqnya.ui.screens.enums.ThemeColor
import me.nekosu.aqnya.ui.screens.enums.ThemeMode
import me.nekosu.aqnya.util.NavBarStyle
import me.nekosu.aqnya.util.PagerAnimationStyle

@Composable
fun AppearanceSection(
    currentThemeMode: ThemeMode,
    currentNavBarStyle: NavBarStyle,
    currentPagerAnimStyle: PagerAnimationStyle,
    currentThemeColor: ThemeColor,
    amoledEnabled: Boolean,
    onThemeChange: (ThemeMode) -> Unit,
    onNavBarStyleChange: (NavBarStyle) -> Unit,
    onPagerAnimationChange: (PagerAnimationStyle) -> Unit,
    onThemeColorChange: (ThemeColor) -> Unit,
    onAmoledChange: (Boolean) -> Unit,
) {
    var themeMenuExpanded by remember { mutableStateOf(false) }
    var themeColorMenuExpanded by remember { mutableStateOf(false) }
    var navBarStyleMenuExpanded by remember { mutableStateOf(false) }
    var pagerAnimMenuExpanded by remember { mutableStateOf(false) }

    CardGroup {
        // 主题模式
        CardItem(index = 0, total = 5) {
            ListItem(
                modifier = Modifier.fillMaxWidth().clickable { themeMenuExpanded = true },
                colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                leadingContent = { Icon(Icons.Outlined.DarkMode, contentDescription = null) },
                headlineContent = {
                    Text(
                        stringResource(R.string.theme_title),
                    )
                },
                supportingContent = { Text(stringResource(currentThemeMode.titleRes)) },
                trailingContent = {
                    Box {
                        DropdownMenu(
                            expanded = themeMenuExpanded,
                            onDismissRequest = { themeMenuExpanded = false },
                            shape = RoundedCornerShape(16.dp),
                        ) {
                            ThemeMode.entries.forEach { mode ->
                                DropdownMenuItem(
                                    text = {
                                        Text(
                                            stringResource(mode.titleRes),
                                            fontWeight = if (currentThemeMode == mode) FontWeight.SemiBold else FontWeight.Normal,
                                        )
                                    },
                                    onClick = {
                                        themeMenuExpanded = false
                                        onThemeChange(mode)
                                    },
                                    trailingIcon = {
                                        if (currentThemeMode == mode) {
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

        // 主题色
        CardItem(index = 1, total = 5) {
            ListItem(
                modifier = Modifier.fillMaxWidth().clickable { themeColorMenuExpanded = true },
                colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                leadingContent = { Icon(Icons.Outlined.Palette, contentDescription = null) },
                headlineContent = {
                    Text(
                        stringResource(R.string.settings_theme_color),
                    )
                },
                supportingContent = { Text(currentThemeColor.label) },
                trailingContent = {
                    Box {
                        DropdownMenu(
                            expanded = themeColorMenuExpanded,
                            onDismissRequest = { themeColorMenuExpanded = false },
                            shape = RoundedCornerShape(16.dp),
                        ) {
                            ThemeColor.entries.forEach { color ->
                                DropdownMenuItem(
                                    text = {
                                        Text(
                                            color.label,
                                            fontWeight =
                                                if (currentThemeColor ==
                                                    color
                                                ) {
                                                    FontWeight.SemiBold
                                                } else {
                                                    FontWeight.Normal
                                                },
                                        )
                                    },
                                    onClick = {
                                        themeColorMenuExpanded = false
                                        onThemeColorChange(color)
                                    },
                                    trailingIcon = {
                                        if (currentThemeColor == color) Icon(Icons.Default.Check, null, Modifier.size(20.dp))
                                    },
                                )
                            }
                        }
                    }
                },
            )
        }

        // AMOLED 纯黑
        CardItem(index = 2, total = 5) {
            ListRow(
                modifier = Modifier.toggleable(value = amoledEnabled, role = Role.Switch, onValueChange = onAmoledChange),
                icon = { Icon(Icons.Outlined.PhoneAndroid, contentDescription = null) },
                headline = {
                    Text(
                        stringResource(R.string.settings_amoled_black),
                    )
                },
                supporting = { Text(stringResource(R.string.settings_amoled_black_summary)) },
                trailing = { Switch(checked = amoledEnabled, onCheckedChange = onAmoledChange) },
            )
        }

        // 导航栏样式
        CardItem(index = 3, total = 5) {
            ListItem(
                modifier = Modifier.fillMaxWidth().clickable { navBarStyleMenuExpanded = true },
                colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                leadingContent = {
                    Icon(Icons.AutoMirrored.Outlined.ViewQuilt, contentDescription = null)
                },
                headlineContent = {
                    Text(
                        stringResource(R.string.navbar_style_title),
                    )
                },
                supportingContent = { Text(stringResource(currentNavBarStyle.titleRes)) },
                trailingContent = {
                    Box {
                        DropdownMenu(
                            expanded = navBarStyleMenuExpanded,
                            onDismissRequest = { navBarStyleMenuExpanded = false },
                            shape = RoundedCornerShape(16.dp),
                        ) {
                            NavBarStyle.entries.forEach { style ->
                                DropdownMenuItem(
                                    text = {
                                        Text(
                                            stringResource(style.titleRes),
                                            fontWeight = if (currentNavBarStyle == style) FontWeight.SemiBold else FontWeight.Normal,
                                        )
                                    },
                                    onClick = {
                                        navBarStyleMenuExpanded = false
                                        onNavBarStyleChange(style)
                                    },
                                    trailingIcon = {
                                        if (currentNavBarStyle == style) {
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

        // 页面切换动画（Pager）
        CardItem(index = 4, total = 5) {
            ListItem(
                modifier = Modifier.fillMaxWidth().clickable { pagerAnimMenuExpanded = true },
                colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                leadingContent = { Icon(Icons.Outlined.Science, contentDescription = null) },
                headlineContent = {
                    Text(
                        stringResource(R.string.pager_anim_title),
                    )
                },
                supportingContent = { Text(stringResource(currentPagerAnimStyle.titleRes)) },
                trailingContent = {
                    Box {
                        DropdownMenu(
                            expanded = pagerAnimMenuExpanded,
                            onDismissRequest = { pagerAnimMenuExpanded = false },
                            shape = RoundedCornerShape(16.dp),
                        ) {
                            PagerAnimationStyle.entries.forEach { style ->
                                DropdownMenuItem(
                                    text = {
                                        Text(
                                            stringResource(style.titleRes),
                                            fontWeight = if (currentPagerAnimStyle == style) FontWeight.SemiBold else FontWeight.Normal,
                                        )
                                    },
                                    onClick = {
                                        pagerAnimMenuExpanded = false
                                        onPagerAnimationChange(style)
                                    },
                                    trailingIcon = {
                                        if (currentPagerAnimStyle == style) {
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
    }
}
