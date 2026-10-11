package me.nekosu.aqnya.util

import me.nekosu.aqnya.ncore

/**
 * 内核 feature（控制 fd 上的 `IOC_FEATURE_*`）的管理器侧封装。
 *
 * 持久化由内核负责：内核在切换时自己写/删 `/data/adb/nksu/feature`，开机再读回来
 * 自动启用。管理器只做即时切换与展示，不碰那个文件。
 */
object KernelFeature {
    /** 与 kernel/manager/feature.h 的 NKSU_FEATURE_SELINUX_HIDE 对应。 */
    const val SELINUX_HIDE = 1

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

    /** 运行时开关；成功返回 true。 */
    fun set(
        id: Int,
        enabled: Boolean,
    ): Boolean {
        val ret = runCatching { ncore.featureSet(id, if (enabled) 1L else 0L) }.getOrNull() ?: return false
        return ret == 0
    }
}
