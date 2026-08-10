package me.nekosu.aqnya

import android.app.Application
import android.content.Context
import android.util.Log
import me.nekosu.aqnya.util.CrashHandler
import me.nekosu.aqnya.util.LocaleHelper
import me.nekosu.aqnya.util.TinkerManager

class Application : Application() {
    override fun attachBaseContext(base: Context) {
        super.attachBaseContext(base)
        val wrappedContext = LocaleHelper.wrap(base, LocaleHelper.savedLanguageTag(base))
    }

    override fun onCreate() {
        super.onCreate()
        CrashHandler.init(this)
        ncore_loader.init()

        // 初始化 Tinker 热更新，捕获异常避免影响主流程
        try {
            TinkerManager.install(this)
        } catch (e: Exception) {
            Log.e("Application", "Tinker install failed: ${e.message}", e)
        }
    }
}