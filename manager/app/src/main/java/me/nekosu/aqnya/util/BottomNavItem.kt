package me.nekosu.aqnya.util

import androidx.annotation.StringRes
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material.icons.automirrored.outlined.List
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Security
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.outlined.Home
import androidx.compose.material.icons.outlined.Security
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.ui.graphics.vector.ImageVector
import me.nekosu.aqnya.R

sealed class BottomNavItem(
    val route: String,
    @StringRes val titleRes: Int,
    val selectedIcon: ImageVector,
    val unselectedIcon: ImageVector,
) {
    object Home : BottomNavItem(
        route = "home",
        titleRes = R.string.nav_home,
        selectedIcon = Icons.Filled.Home,
        unselectedIcon = Icons.Outlined.Home,
    )

    object History : BottomNavItem(
        route = "app",
        titleRes = R.string.nav_apps,
        selectedIcon = Icons.AutoMirrored.Filled.List,
        unselectedIcon = Icons.AutoMirrored.Outlined.List,
    )

    object FmacRules : BottomNavItem(
        route = "rules",
        titleRes = R.string.nav_rules,
        selectedIcon = Icons.Filled.Security,
        unselectedIcon = Icons.Outlined.Security,
    )

    object Settings : BottomNavItem(
        route = "settings",
        titleRes = R.string.nav_settings,
        selectedIcon = Icons.Filled.Settings,
        unselectedIcon = Icons.Outlined.Settings,
    )

    companion object {
        fun items(showRules: Boolean): List<BottomNavItem> =
            buildList {
                add(Home)
                add(History)
                if (showRules) add(FmacRules)
                add(Settings)
            }
    }
}
