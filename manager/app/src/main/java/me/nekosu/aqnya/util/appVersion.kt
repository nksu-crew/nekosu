package me.nekosu.aqnya.util

import android.content.Context
import android.content.pm.PackageManager
import android.os.Build

fun getAppVersion(context: Context): String =
    try {
        val pm = context.packageManager
        val pkgName = context.packageName

        val pkgInfo =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                pm.getPackageInfo(pkgName, PackageManager.PackageInfoFlags.of(0))
            } else {
                @Suppress("DEPRECATION")
                pm.getPackageInfo(pkgName, 0)
            }

        val versionName = pkgInfo.versionName ?: "unknown"
        val versionCode =
            if (Build.VERSION.SDK_INT >= 28) {
                pkgInfo.longVersionCode
            } else {
                @Suppress("DEPRECATION")
                pkgInfo.versionCode.toLong()
            }

        "$versionName ($versionCode)"
    } catch (e: Exception) {
        "unknown"
    }
