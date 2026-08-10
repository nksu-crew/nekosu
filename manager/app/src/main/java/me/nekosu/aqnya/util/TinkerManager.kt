package me.nekosu.aqnya.util

import android.app.Application
import android.content.Context
import android.util.Log
import com.tencent.tinker.lib.library.TinkerLoadLibrary
import com.tencent.tinker.lib.tinker.Tinker
import com.tencent.tinker.lib.tinker.TinkerInstaller
import com.tencent.tinker.loader.app.ApplicationLike
import com.tencent.tinker.loader.app.DefaultApplicationLike
import com.tencent.tinker.loader.shareutil.ShareConstants
import com.tencent.tinker.loader.shareutil.SharePatchFileUtil
import com.tencent.tinker.loader.shareutil.ShareTinkerInternals
import java.io.File

/**
 * Tinker 热更新管理器
 * 封装 Tinker SDK 的初始化、补丁加载、清理与状态查询。
 */
object TinkerManager {
    private const val TAG = "TinkerManager"

    /** 补丁存储目录名 */
    private const val PATCH_DIR = "tinker_patches"

    /** 标识是否已执行 install */
    @Volatile
    private var installed = false

    /**
     * 必须在 Application.attachBaseContext 之后、onCreate 之前调用。
     * 不能直接在 attachBaseContext 中调用，因为 Tinker 需要已初始化的 Application。
     */
    fun install(application: Application) {
        if (installed) {
            Log.w(TAG, "Tinker already installed, skipping")
            return
        }

        val appLike: ApplicationLike = object : DefaultApplicationLike(
            application,
            ShareTinkerInternals.getTinkerFlags(),
            ShareConstants.TINKER_ENABLE_ALL,
            null, // loaderClassName, keep default
            null, // delegateClassName, keep default
            false, // useVerifyMd5WhenLoad
            false, // useDelegateLastClassLoader
        ) {
            override fun onBaseContextAttached(base: Context) {
                super.onBaseContextAttached(base)
                // 允许 Tinker 加载 so 库
                TinkerLoadLibrary.installNavitveLibraryABI(base, ShareConstants.CPU_ABI)
            }
        }

        TinkerInstaller.install(appLike)
        installed = true
        Log.i(TAG, "Tinker installed successfully")

        // 已有补丁的话，立即加载
        loadExistingPatch(application)
    }

    /**
     * 加载已下载的补丁文件。
     * 若补丁加载成功，下次冷启动自动生效；
     * 若需要立即生效，调用者应在加载成功后提示用户重启。
     *
     * @return true 表示补丁文件存在且校验通过，已提交加载
     */
    fun loadPatch(
        context: Context,
        patchFile: File,
    ): LoadResult {
        if (!patchFile.exists() || !patchFile.isFile) {
            return LoadResult.Error("补丁文件不存在: ${patchFile.path}")
        }

        val tinker = Tinker.with(context)
        if (!tinker.isTinkerLoaded) {
            Log.w(TAG, "Tinker not loaded, patch will take effect after kill-and-restart")
        }

        return try {
            TinkerInstaller.onReceiveUpgradePatch(context, patchFile.absolutePath)
            Log.i(TAG, "Patch loaded: ${patchFile.name}")
            LoadResult.Success(patchFile.name)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to load patch: ${e.message}", e)
            LoadResult.Error(e.message ?: "未知错误")
        }
    }

    /**
     * 从补丁目录加载已存在的补丁。
     */
    private fun loadExistingPatch(context: Context) {
        val dir = getPatchDir(context)
        if (!dir.exists()) return

        val patches = dir.listFiles { f -> f.isFile && f.name.endsWith(".apk") } ?: return
        if (patches.isEmpty()) return

        val latest = patches.maxByOrNull { it.lastModified() } ?: return
        Log.i(TAG, "Found existing patch: ${latest.name}")
        val result = loadPatch(context, latest)
        if (result is LoadResult.Success) {
            // 补丁成功提交；但已加载过则无需重复提示
            Log.i(TAG, "Existing patch submitted: ${latest.name}")
        } else {
            Log.w(TAG, "Existing patch load failed, deleting: ${latest.name}")
            latest.delete()
        }
    }

    /**
     * 清理所有补丁。
     */
    fun cleanPatches(context: Context) {
        val tinker = Tinker.with(context)
        SharePatchFileUtil.deleteDir(SharePatchFileUtil.getPatchDirectory(context))
        tinker.cleanPatch()
        Log.i(TAG, "All patches cleaned")
    }

    /**
     * 检查是否有补丁待生效（已提交但尚未加载）。
     */
    fun isPatchPending(context: Context): Boolean {
        val tinker = Tinker.with(context)
        return !tinker.isTinkerLoaded && tinker.tinkerLoadResult?.useInterpretMode == false
    }

    /**
     * 获取补丁存储目录。
     */
    fun getPatchDir(context: Context): File {
        val dir = File(context.filesDir, PATCH_DIR)
        if (!dir.exists()) dir.mkdirs()
        return dir
    }

    /**
     * 将下载的补丁文件安全移动到补丁目录。
     * @return 移动后的文件，失败返回 null
     */
    fun moveToPatchDir(
        context: Context,
        source: File,
        patchName: String,
    ): File? {
        val target = File(getPatchDir(context), patchName)
        return try {
            if (target.exists()) target.delete()
            source.copyTo(target, overwrite = true)
            source.delete()
            target
        } catch (e: Exception) {
            Log.e(TAG, "Failed to move patch: ${e.message}", e)
            null
        }
    }

    /**
     * 补丁加载结果。
     */
    sealed class LoadResult {
        data class Success(val patchName: String) : LoadResult()
        data class Error(val message: String) : LoadResult()
    }
}
