package me.nekosu.aqnya.ui.screens

import android.app.Application
import androidx.compose.animation.AnimatedContentTransitionScope
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Scaffold
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavBackStackEntry
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import kotlinx.coroutines.delay
import me.nekosu.aqnya.ui.animation.AnimatedBottomNavBar
import me.nekosu.aqnya.ui.animation.PageTransitions
import me.nekosu.aqnya.ui.animation.ScrollAnimations
import me.nekosu.aqnya.util.AppPermission
import me.nekosu.aqnya.util.BottomNavItem
import me.nekosu.aqnya.util.DebugPreferences
import me.nekosu.aqnya.util.MiuiPermissionUtils
import me.nekosu.aqnya.util.NavBarStyle
import me.nekosu.aqnya.util.rememberPermissionState

@Composable
fun BottomNavigationBar(
    items: List<BottomNavItem>,
    selectedIndex: Int,
    onTabClick: (Int) -> Unit,
    style: NavBarStyle,
) {
    when (style) {
        NavBarStyle.FLOATING -> FloatingBottomNavigationBar(items, selectedIndex, onTabClick)
        NavBarStyle.NORMAL -> NormalBottomNavigationBar(items, selectedIndex, onTabClick)
    }
}

// 根据 Tab 在 navItems 中的相对位置决定滑入/滑出方向，
// 非 Tab 路由（about/settings 详情等）一律走 fade，交给 PageTransitions 处理。
private fun tabTransitionEnter(navItems: List<BottomNavItem>): AnimatedContentTransitionScope<NavBackStackEntry>.() -> EnterTransition =
    {
        val routes = navItems.map { it.route }
        val initialRoute = initialState.destination.route
        val targetRoute = targetState.destination.route

        if (initialRoute in routes && targetRoute in routes) {
            val initialIndex = navItems.indexOfFirst { it.route == initialRoute }
            val targetIndex = navItems.indexOfFirst { it.route == targetRoute }
            if (targetIndex > initialIndex) {
                slideInHorizontally(animationSpec = tween(280), initialOffsetX = { it / 4 }) + fadeIn(tween(280))
            } else {
                slideInHorizontally(animationSpec = tween(280), initialOffsetX = { -it / 4 }) + fadeIn(tween(280))
            }
        } else {
            fadeIn(animationSpec = tween(220))
        }
    }

private fun tabTransitionExit(navItems: List<BottomNavItem>): AnimatedContentTransitionScope<NavBackStackEntry>.() -> ExitTransition =
    {
        val routes = navItems.map { it.route }
        val initialRoute = initialState.destination.route
        val targetRoute = targetState.destination.route

        if (initialRoute in routes && targetRoute in routes) {
            val initialIndex = navItems.indexOfFirst { it.route == initialRoute }
            val targetIndex = navItems.indexOfFirst { it.route == targetRoute }
            if (targetIndex > initialIndex) {
                slideOutHorizontally(animationSpec = tween(280), targetOffsetX = { -it / 4 }) + fadeOut(tween(280))
            } else {
                slideOutHorizontally(animationSpec = tween(280), targetOffsetX = { it / 4 }) + fadeOut(tween(280))
            }
        } else {
            fadeOut(animationSpec = tween(220))
        }
    }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen() {
    val navController = rememberNavController()
    val context = LocalContext.current

    val homeViewModel: HomeViewModel =
        viewModel(
            factory = HomeViewModelFactory(context.applicationContext as Application),
        )
    val showRules by DebugPreferences.showRulesFlow(context).collectAsState(initial = false)
    val navItems = remember(showRules) { BottomNavItem.items(showRules) }

    val navBarStyleValue by DebugPreferences.navBarStyleFlow(context).collectAsState(initial = 0)
    val navBarStyle = NavBarStyle.fromValue(navBarStyleValue)

    val miuiAppsPermState = rememberPermissionState(AppPermission.MIUI_GET_INSTALLED_APPS)

    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentRoute = navBackStackEntry?.destination?.route
    val bottomBarRoutes = remember(navItems) { navItems.map { it.route }.toSet() }
    val showBottomBar = currentRoute in bottomBarRoutes
    val selectedIndex = navItems.indexOfFirst { it.route == currentRoute }.coerceAtLeast(0)

    var navBarVisible by remember { mutableStateOf(true) }
    val nestedScrollConnection =
        ScrollAnimations.rememberNavBarScrollConnection(
            onVisibilityChange = { visible -> navBarVisible = visible },
        )

    LaunchedEffect(currentRoute) { navBarVisible = true }

    LaunchedEffect(currentRoute) {
        if (currentRoute == BottomNavItem.Home.route) {
            delay(300)
            homeViewModel.refresh()
        }
    }

    LaunchedEffect(Unit) {
        if (MiuiPermissionUtils.isSupportedOnThisDevice(context) &&
            !MiuiPermissionUtils.isGranted(context)
        ) {
            miuiAppsPermState.launchRequest()
        }
    }

    // 规则页被隐藏时，如果用户正停在规则页，自动退回首页
    LaunchedEffect(showRules) {
        if (!showRules && currentRoute == BottomNavItem.FmacRules.route) {
            navController.navigate(BottomNavItem.Home.route) {
                popUpTo(BottomNavItem.Home.route) { inclusive = true }
                launchSingleTop = true
            }
        }
    }

    val onTabClick: (Int) -> Unit = { index ->
        val target = navItems.getOrNull(index)?.route
        if (target != null && target != currentRoute) {
            navController.navigate(target) {
                // 回到 Tab 根，保存/恢复各 Tab 的滚动状态等
                popUpTo(BottomNavItem.Home.route) {
                    saveState = true
                }
                launchSingleTop = true
                restoreState = true
            }
        }
    }

    Scaffold(
        contentWindowInsets =
            WindowInsets.safeDrawing.only(
                WindowInsetsSides.Bottom + WindowInsetsSides.Horizontal,
            ),
        bottomBar = {
            if (navBarStyle == NavBarStyle.NORMAL) {
                AnimatedBottomNavBar(
                    visible = showBottomBar,
                    style = navBarStyle,
                    items = navItems,
                    selectedIndex = selectedIndex,
                    onTabClick = onTabClick,
                    content = { items, selIndex, onClick, style ->
                        BottomNavigationBar(
                            items = items,
                            selectedIndex = selIndex,
                            onTabClick = onClick,
                            style = style,
                        )
                    },
                )
            }
        },
    ) { innerPadding ->
        Box(
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(innerPadding),
        ) {
            NavHost(
                navController = navController,
                startDestination = BottomNavItem.Home.route,
                modifier =
                    Modifier
                        .fillMaxSize()
                        .nestedScroll(nestedScrollConnection),
            ) {
                composable(
                    route = BottomNavItem.Home.route,
                    enterTransition = tabTransitionEnter(navItems),
                    exitTransition = tabTransitionExit(navItems),
                ) {
                    HomeScreen(
                        viewModel = homeViewModel,
                        onNavigateToApps = {
                            onTabClick(navItems.indexOfFirst { it is BottomNavItem.History })
                        },
                        onNavigateToRules = {
                            onTabClick(navItems.indexOfFirst { it is BottomNavItem.FmacRules })
                        },
                    )
                }

                composable(
                    route = BottomNavItem.History.route,
                    enterTransition = tabTransitionEnter(navItems),
                    exitTransition = tabTransitionExit(navItems),
                ) {
                    AppListScreen(
                        navController = navController,
                        extraBottomPadding = 12.dp,
                    )
                }

                composable(
                    route = BottomNavItem.FmacRules.route,
                    enterTransition = tabTransitionEnter(navItems),
                    exitTransition = tabTransitionExit(navItems),
                ) {
                    RulesScreen()
                }

                composable(
                    route = BottomNavItem.Settings.route,
                    enterTransition = tabTransitionEnter(navItems),
                    exitTransition = tabTransitionExit(navItems),
                ) {
                    SettingsScreen(navController)
                }

                composable(
                    route = "about",
                    enterTransition = { PageTransitions.defaultEnter },
                    exitTransition = { PageTransitions.defaultExit },
                ) {
                    AboutScreen(navController)
                }

                composable(
                    route = "open_source",
                    enterTransition = { PageTransitions.defaultEnter },
                    exitTransition = { PageTransitions.defaultExit },
                ) {
                    OpenSourceScreen(navController)
                }

                composable(
                    route = "debug_settings",
                    enterTransition = { PageTransitions.defaultEnter },
                    exitTransition = { PageTransitions.defaultExit },
                ) {
                    DebugSettingsScreen(navController)
                }

                composable("app_detail/{packageName}") { backStackEntry ->
                    val pkg = backStackEntry.arguments?.getString("packageName")
                    if (pkg == null) {
                        LaunchedEffect(Unit) { navController.popBackStack() }
                        return@composable
                    }
                    val appViewModel: AppViewModel = viewModel(factory = AppViewModelFactory(context.applicationContext))
                    val app = appViewModel.allApps.find { it.packageName == pkg }

                    if (app != null) {
                        AppDetailScreen(
                            app = app,
                            config = appViewModel.appConfigs[pkg],
                            onSave = { appViewModel.setAppConfig(app, it) },
                            onBack = { navController.popBackStack() },
                            navController = navController,
                        )
                    } else {
                        LaunchedEffect(Unit) {
                            appViewModel.loadApps()
                            if (appViewModel.allApps.none { it.packageName == pkg }) {
                                navController.popBackStack()
                            }
                        }
                        Box(
                            modifier = Modifier.fillMaxSize(),
                            contentAlignment = Alignment.Center,
                        ) {
                            LoadingState()
                        }
                    }
                }
            }

            if (navBarStyle != NavBarStyle.NORMAL) {
                AnimatedBottomNavBar(
                    visible = showBottomBar && navBarVisible,
                    style = navBarStyle,
                    items = navItems,
                    selectedIndex = selectedIndex,
                    onTabClick = onTabClick,
                    modifier = Modifier.align(Alignment.BottomCenter),
                    content = { items, selIndex, onClick, style ->
                        BottomNavigationBar(
                            items = items,
                            selectedIndex = selIndex,
                            onTabClick = onClick,
                            style = style,
                        )
                    },
                )
            }
        }
    }
}

@Preview(showBackground = true)
@Composable
fun MainScreenPreview() {
    MainScreen()
}
