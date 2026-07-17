package me.nekosu.aqnya.util

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.core.content.ContextCompat

enum class AppPermission(
    val manifest: String,
    val minSdk: Int? = null,
) {
    POST_NOTIFICATIONS(
        manifest = Manifest.permission.POST_NOTIFICATIONS,
        minSdk = Build.VERSION_CODES.TIRAMISU, // Android 13+
    ),
    READ_MEDIA_IMAGES(
        manifest = Manifest.permission.READ_MEDIA_IMAGES,
        minSdk = Build.VERSION_CODES.TIRAMISU,
    ),
    QUERY_ALL_PACKAGES(
        manifest = Manifest.permission.QUERY_ALL_PACKAGES,
        minSdk = Build.VERSION_CODES.R,
    ),

    MIUI_GET_INSTALLED_APPS(
        manifest = "com.android.permission.GET_INSTALLED_APPS",
    ),
    ;

    val isApplicable: Boolean
        get() = minSdk == null || Build.VERSION.SDK_INT >= minSdk
}

object PermissionUtils {
    fun isGranted(
        context: Context,
        permission: AppPermission,
    ): Boolean {
        if (!permission.isApplicable) return true
        return ContextCompat.checkSelfPermission(
            context,
            permission.manifest,
        ) == PackageManager.PERMISSION_GRANTED
    }

    fun allGranted(
        context: Context,
        permissions: Collection<AppPermission>,
    ): Boolean = permissions.all { isGranted(context, it) }

    fun deniedPermissions(
        context: Context,
        permissions: Collection<AppPermission>,
    ): List<AppPermission> = permissions.filter { !isGranted(context, it) }

    fun isPermanentlyDenied(
        activity: Activity,
        permission: AppPermission,
    ): Boolean {
        if (!permission.isApplicable) return false
        return !activity.shouldShowRequestPermissionRationale(permission.manifest) &&
            !isGranted(activity, permission)
    }

    fun openAppSettings(context: Context) {
        val intent =
            Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
                data = Uri.fromParts("package", context.packageName, null)
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
        context.startActivity(intent)
    }
}

object MiuiPermissionUtils {
    private const val PERMISSION = "com.android.permission.GET_INSTALLED_APPS"
    private const val MIUI_SECURITY_PKG = "com.lbe.security.miui"

    fun isSupportedOnThisDevice(context: Context): Boolean =
        try {
            val info = context.packageManager.getPermissionInfo(PERMISSION, 0)
            info.packageName == MIUI_SECURITY_PKG
        } catch (_: PackageManager.NameNotFoundException) {
            false
        }

    fun isGranted(context: Context): Boolean {
        if (!isSupportedOnThisDevice(context)) return true
        return ContextCompat.checkSelfPermission(context, PERMISSION) ==
            PackageManager.PERMISSION_GRANTED
    }
}

class PermissionState(
    val permission: AppPermission,
    initialGranted: Boolean,
) {
    var isGranted by mutableStateOf(initialGranted)
        internal set

    var isPermanentlyDenied by mutableStateOf(false)
        internal set

    var launchRequest: () -> Unit = {}
        internal set
}

@Composable
fun rememberPermissionState(
    permission: AppPermission,
    onResult: (granted: Boolean) -> Unit = {},
): PermissionState {
    val context = androidx.compose.ui.platform.LocalContext.current

    val state =
        remember(permission) {
            PermissionState(
                permission = permission,
                initialGranted = PermissionUtils.isGranted(context, permission),
            )
        }

    val launcher =
        rememberLauncherForActivityResult(
            contract = ActivityResultContracts.RequestPermission(),
        ) { granted ->
            state.isGranted = granted
            if (!granted) {
                val activity = context as? Activity
                state.isPermanentlyDenied =
                    !PermissionUtils.isGranted(context, permission) &&
                    activity?.shouldShowRequestPermissionRationale(permission.manifest) == false
            }
            onResult(granted)
        }

    LaunchedEffect(permission) {
        state.launchRequest = { if (!state.isGranted) launcher.launch(permission.manifest) }
    }

    return state
}

class MultiplePermissionsState(
    val permissions: List<AppPermission>,
    initialGranted: Map<AppPermission, Boolean>,
) {
    var statuses by mutableStateOf(initialGranted)
        internal set

    val allGranted: Boolean get() = statuses.values.all { it }

    val deniedPermissions: List<AppPermission>
        get() = statuses.filterValues { !it }.keys.toList()

    var launchRequest: () -> Unit = {}
        internal set
}

@Composable
fun rememberMultiplePermissionsState(
    permissions: List<AppPermission>,
    onResult: (statuses: Map<AppPermission, Boolean>) -> Unit = {},
): MultiplePermissionsState {
    val context = androidx.compose.ui.platform.LocalContext.current

    val applicablePermissions =
        remember(permissions) {
            permissions.filter { it.isApplicable }
        }

    val state =
        remember(permissions) {
            MultiplePermissionsState(
                permissions = applicablePermissions,
                initialGranted =
                    applicablePermissions.associateWith {
                        PermissionUtils.isGranted(context, it)
                    },
            )
        }

    val launcher =
        rememberLauncherForActivityResult(
            contract = ActivityResultContracts.RequestMultiplePermissions(),
        ) { results ->
            val mapped =
                results.entries.associate { (manifest, granted) ->
                    applicablePermissions.first { it.manifest == manifest } to granted
                }
            state.statuses = mapped
            onResult(mapped)
        }

    LaunchedEffect(permissions) {
        state.launchRequest = {
            val denied = state.deniedPermissions.map { it.manifest }.toTypedArray()
            if (denied.isNotEmpty()) launcher.launch(denied)
        }
    }

    return state
}
