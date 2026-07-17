package me.nekosu.aqnya.util

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import okhttp3.OkHttpClient
import okhttp3.Request

@Serializable
data class GitHubRelease(
    @SerialName("tag_name") val tagName: String,
    @SerialName("name") val releaseName: String,
)

object UpdateChecker {
    private val client = OkHttpClient()
    private val json = Json { ignoreUnknownKeys = true }

    suspend fun fetchLatestVersion(
        owner: String,
        repo: String,
    ): String? =
        withContext(Dispatchers.IO) {
            try {
                val request =
                    Request
                        .Builder()
                        .url("https://api.github.com/repos/$owner/$repo/releases/latest")
                        .header("User-Agent", "MyApp/1.0")
                        .build()

                client.newCall(request).execute().use { resp ->
                    if (!resp.isSuccessful) {
                        Log.e("UpdateCheck", "HTTP error: ${resp.code}")
                        return@withContext null
                    }

                    val body = resp.body.string()
                    if (body.isBlank()) {
                        Log.e("UpdateCheck", "Response body is empty")
                        return@withContext null
                    }

                    Log.d("UpdateCheck", "GitHub raw JSON: $body")

                    json.decodeFromString<GitHubRelease>(body).releaseName
                }
            } catch (e: Exception) {
                Log.e("UpdateCheck", "Error checking update", e)
                null
            }
        }
}
