package me.nekosu.aqnya

import android.app.Application
import android.content.Context
import me.nekosu.aqnya.util.CrashHandler
import me.nekosu.aqnya.util.LocaleHelper

class Application : Application() {
    override fun attachBaseContext(base: Context) {
        super.attachBaseContext(LocaleHelper.wrap(base, LocaleHelper.savedLanguageTag(base)))
    }

    override fun onCreate() {
        super.onCreate()
        CrashHandler.init(this)
        ncore_loader.init()
    }
}
