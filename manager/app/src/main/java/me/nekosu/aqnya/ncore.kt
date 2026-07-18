package me.nekosu.aqnya

import android.util.Log

object ncore_loader {
    private const val TAG = "ncore_loader"
    private var isLibraryLoaded = false

    fun init() {
        if (isLibraryLoaded) return

        try {
            System.loadLibrary("jni")
            isLibraryLoaded = true
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "Failed to load ncore library: ${e.message}")
        }
    }
}

object ncore {
    external fun helloLog()

    external fun ctl(value: Int): Int

    external fun adduid(value: Int): Int

    external fun deluid(value: Int): Int

    external fun hasuid(value: Int): Int

    external fun addRule(
        path: String,
        statusBits: Long,
    ): Int

    external fun delRule(path: String): Int

    external fun setCap(
        uid: Int,
        caps: Long,
    ): Int

    external fun getCap(uid: Int): Long

    external fun delCap(uid: Int): Int

    external fun addSelinuxRule(
        src: String?,
        tgt: String?,
        cls: String?,
        perm: String?,
        effect: Int,
        invert: Boolean,
    ): Int

    external fun setProfile(
        uid: Int,
        caps: Long,
        domain: String,
        namespace: Int,
    ): Int
    
    external fun isGki(): Boolean
    external fun kernelVersion(): String?
}
