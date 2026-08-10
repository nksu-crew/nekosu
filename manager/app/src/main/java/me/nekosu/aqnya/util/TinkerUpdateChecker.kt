package me.nekosu.aqnya.util

import android.content.Context
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.File
import java.io.FileOutputStream

/**
 * Tinker 热更新补丁检查器
 * 从 GitHub Release 获取补丁信息并下载补丁 APK。
 */
object TinkerUpdateChecker {
    private const val TAG = "TinkerUpdateChecker"

    private val client = OkHttpClient()
    private val json = Json { ignoreUnknownKeys = true }

    /**
     * 查询指定仓库的最新 release 中的补丁信息。
     * @param owner GitHub 仓库所有者
     * @param repo  GitHub 仓库名
     */
    suspend fun fetchLatestPatch(
        owner: String,
        repo: String,
    ): PatchInfo? =
        withContext(Dispatchers.IO) {
            try {
                val releaseUrl = "https://api.github.com/repos/$owner/$repo/releases/latest"
                val request =
                    Request.Builder()
                        .url(releaseUrl)
                        .header("User-Agent", "NekoSU-Manager")
                        .build()

                client.newCall(request).execute().use { resp ->
                    if (!resp.isSuccessful) {
                        Log.e(TAG, "HTTP error: ${resp.code}")
                        return@withContext null
                    }

                    val body = resp.body.string()
                    if (body.isBlank()) {
                        Log.e(TAG, "Response body empty")
                        return@withContext null
                    }

                    val release = json.decodeFromString<GitHubRelease>(body)

                    // 寻找补丁资源（命名约定：patch_signed.apk 或以 _patch.apk 结尾）
                    val patchAsset = release.assets?.firstOrNull { asset ->
                        asset.name.endsWith("_patch.apk") ||
                            asset.name == "patch_signed.apk"
                    }

                    if (patchAsset == null) {
                        Log.i(TAG, "No patch asset found in release ${release.tagName}")
                        return@withContext null
                    }

                    PatchInfo(
                        tagName = release.tagName,
                        releaseName = release.name ?: release.tagName,
                        patchName = patchAsset.name,
                        downloadUrl = patchAsset.browserDownloadUrl,
                        size = patchAsset.size,
                    )
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error checking patch: ${e.message}", e)
                null
            }
        }

    /**
     * 下载补丁文件到临时目录。
     * @return 下载后的临时文件，失败返回 null
     */
    suspend fun downloadPatch(
        context: Context,
        patchInfo: PatchInfo,
    ): File? =
        withContext(Dispatchers.IO) {
            try {
                val tmpFile = File(context.cacheDir, patchInfo.patchName)
                if (tmpFile.exists()) tmpFile.delete()

                val request =
                    Request.Builder()
                        .url(patchInfo.downloadUrl)
                        .header("User-Agent", "NekoSU-Manager")
                        .build()

                client.newCall(request).execute().use { resp ->
                    if (!resp.isSuccessful) {
                        Log.e(TAG, "Download HTTP error: ${resp.code}")
                        return@withContext null
                    }

                    val body = resp.body ?: return@withContext null
                    val total = body.contentLength()

                    FileOutputStream(tmpFile).use { fos ->
                        body.byteStream().use { input ->
                            val buffer = ByteArray(8192)
                            var read: Int
                            var downloaded = 0L
                            while (input.read(buffer).also { read = it } != -1) {
                                fos.write(buffer, 0, read)
                                downloaded += read
                            }
                        }
                    }

                    Log.i(TAG, "Patch downloaded: ${tmpFile.absolutePath} " +
                        "(${tmpFile.length()} bytes)")
                    tmpFile
                }
            } catch (e: Exception) {
                Log.e(TAG, "Download failed: ${e.message}", e)
                null
            }
        }

    /**
     * 补丁信息数据类。
     */
    data class PatchInfo(
        val tagName: String,
        val releaseName: String,
        val patchName: String,
        val downloadUrl: String,
        val size: Long,
    )

    // ── GitHub API 模型 ──

    @Serializable
    private data class GitHubRelease(
        @SerialName("tag_name") val tagName: String,
        @SerialName("name") val name: String? = null,
        @SerialName("assets") val assets: List<GitHubAsset>? = null,
    )

    @Serializable
    private data class GitHubAsset(
        @SerialName("name") val name: String,
        @SerialName("browser_download_url") val browserDownloadUrl: String,
        @SerialName("size") val size: Long,
    )
}
