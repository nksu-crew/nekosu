package me.nekosu.aqnya.util

import me.nekosu.aqnya.ncore

/**
 * 内核 feature（控制 fd 上的 `IOC_FEATURE_*`）的管理器侧封装。
 *
 * 内核只在开机时读 `/data/adb/nksu/feature` 决定默认开关，所以运行时切换要同时
 * 落盘这个标志文件，重启后才保持。
 */
object KernelFeature {
    /** 与 kernel/manager/feature.h 的 NKSU_FEATURE_SELINUX_HIDE 对应。 */
    const val SELINUX_HIDE = 1

    private const val BOOT_FLAG = "/data/adb/nksu/feature"

    data class Info(
        val id: Int,
        val name: String,
        val enabled: Boolean,
    )

    /** 内核当前支持的 feature 列表；内核不可用时为空表。 */
    fun list(): List<Info> =
        runCatching { ncore.featureList() }
            .getOrNull()
            ?.lineSequence()
            ?.mapNotNull { line ->
                val parts = line.trim().split(Regex("\\s+"))
                if (parts.size < 3) return@mapNotNull null
                val id = parts[0].toIntOrNull() ?: return@mapNotNull null
                Info(
                    id = id,
                    name = parts[1],
                    enabled = parts[2].toLongOrNull() != 0L,
                )
            }?.toList()
            .orEmpty()

    /** 运行时开关（并按需落盘开机标志）；成功返回 true。 */
    fun set(
        id: Int,
        enabled: Boolean,
    ): Boolean {
        val ret =
            runCatching { ncore.featureSet(id, if (enabled) 1L else 0L) }.getOrNull()
                ?: return false
        if (ret != 0) return false

        // SELinux 隐藏靠这个文件在开机时自动启用。
        if (id == SELINUX_HIDE) {
            runCatching { RootShell.exec(if (enabled) "touch $BOOT_FLAG" else "rm -f $BOOT_FLAG") }
        }
        return true
    }
}
