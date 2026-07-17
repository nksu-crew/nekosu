package me.nekosu.aqnya.util

import android.content.Context
import android.os.Build
import android.os.Environment
import java.io.File
import java.io.FileOutputStream
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class CrashHandler(
    private val context: Context,
) : Thread.UncaughtExceptionHandler {
    private val defaultHandler = Thread.getDefaultUncaughtExceptionHandler()

    override fun uncaughtException(
        thread: Thread,
        throwable: Throwable,
    ) {
        handleException(throwable)
        defaultHandler?.uncaughtException(thread, throwable)
    }

    private fun handleException(throwable: Throwable) {
        val logDir =
            File(
                Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
                "nekosu_crashes",
            )
        if (!logDir.exists()) {
            logDir.mkdirs()
        }
        val timestamp = SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.US).format(Date())
        val logFile = File(logDir, "crash-$timestamp.log")

        try {
            FileOutputStream(logFile).use { fos ->
                PrintWriter(fos).use { printWriter ->
                    printWriter.println("Crash Time: $timestamp")
                    printWriter.println("Device Information:")
                    printWriter.println("===================")
                    printWriter.println("Manufacturer: ${Build.MANUFACTURER}")
                    printWriter.println("Model: ${Build.MODEL}")
                    printWriter.println("Android Version: ${Build.VERSION.RELEASE}")
                    printWriter.println("SDK Version: ${Build.VERSION.SDK_INT}")
                    printWriter.println("Stack Trace:")
                    printWriter.println("============")
                    val sw = StringWriter()
                    val pw = PrintWriter(sw)
                    throwable.printStackTrace(pw)
                    printWriter.write(sw.toString())
                }
            }
        } catch (e: Exception) {
            // Ignore
        }
    }

    companion object {
        fun init(context: Context) {
            if (me.nekosu.aqnya.BuildConfig.DEBUG) {
                Thread.setDefaultUncaughtExceptionHandler(CrashHandler(context))
            }
        }
    }
}
