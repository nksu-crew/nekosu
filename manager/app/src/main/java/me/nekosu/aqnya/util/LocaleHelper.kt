package me.nekosu.aqnya.util

import android.app.Activity
import android.content.Context
import android.content.res.Configuration
import androidx.annotation.StringRes
import me.nekosu.aqnya.R
import java.util.Locale

object LocaleHelper {
    private const val PREFS_NAME = "app_preferences"
    private const val KEY_LANG = "language_tag"

    fun savedLanguageTag(context: Context): String =
        context
            .getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .getString(KEY_LANG, "") ?: ""

    fun saveLanguageTag(
        context: Context,
        tag: String,
    ) {
        context
            .getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_LANG, tag)
            .apply()
    }

    fun wrap(
        context: Context,
        languageTag: String,
    ): Context {
        if (languageTag.isBlank()) return context
        val locale = Locale.forLanguageTag(languageTag)
        Locale.setDefault(locale)
        val config = Configuration(context.resources.configuration).apply { setLocale(locale) }
        return context.createConfigurationContext(config)
    }

    /** 重建 Activity 以应用语言变更（不杀进程） */
    fun applyLanguage(activity: Activity) {
        activity.recreate()
    }

    /** 语言选项列表（使用字符串资源 ID，由 Composable 解析） */
    val availableLanguages =
        listOf(
            LanguageOption("", R.string.language_system),
            LanguageOption("zh-CN", R.string.language_zh_cn),
            LanguageOption("en", R.string.language_en),
        )

    data class LanguageOption(
        val tag: String,
        @StringRes val labelRes: Int,
    )
}
