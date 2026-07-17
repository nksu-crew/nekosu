package me.nekosu.aqnya

import android.content.Context
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import me.nekosu.aqnya.ui.screens.MainScreen
import me.nekosu.aqnya.ui.theme.NekosuTheme
import me.nekosu.aqnya.util.LocaleHelper

class MainActivity : ComponentActivity() {
    override fun attachBaseContext(newBase: Context) {
        super.attachBaseContext(LocaleHelper.wrap(newBase, LocaleHelper.savedLanguageTag(newBase)))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            NekosuTheme {
                MainScreen()
            }
        }
    }
}
