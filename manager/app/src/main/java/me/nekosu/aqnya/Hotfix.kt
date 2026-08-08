package me.nekosu.aqnya

import android.content.Context
import android.os.Build
import android.util.Log
import java.io.File
import java.lang.reflect.Field

object HotFix {

    private const val TAG = "HotFix"
    private const val DEX_NAME = "patch.dex"

    fun loadPatch(context: Context, dexFileName: String = DEX_NAME) {
        val dexFile = File(context.filesDir, dexFileName)
        if (!dexFile.exists()) {
            return
        }

        try {
            injectDex(context, dexFile)
            Log.i(TAG, "loaded hotfix：${dexFile.absolutePath}")
        } catch (e: Exception) {
            Log.e(TAG, "failed to load hotfix", e)
        }
    }

    private fun injectDex(context: Context, dexFile: File) {
        val classLoader = context.classLoader

        val pathListField = getDeclaredField(classLoader, "pathList")
        val pathList = pathListField.get(classLoader)

        val dexElementsField = getDeclaredField(pathList, "dexElements")
        val oldDexElements = dexElementsField.get(pathList) as Array<*>

        val newDexElement = createDexElement(pathList, dexFile)

        val mergedElements = arrayOf(newDexElement, *oldDexElements)
        dexElementsField.set(pathList, mergedElements)
    }

    private fun getDeclaredField(obj: Any, fieldName: String): Field {
        var clazz: Class<*>? = obj.javaClass
        while (clazz != null) {
            try {
                val field = clazz.getDeclaredField(fieldName)
                field.isAccessible = true
                return field
            } catch (e: NoSuchFieldException) {
                clazz = clazz.superclass
            }
        }
        throw NoSuchFieldException("Field $fieldName not found in ${obj.javaClass}")
    }

    private fun createDexElement(pathList: Any, dexFile: File): Any {
        val method = pathList.javaClass.getDeclaredMethod(
            "makePathElements",
            List::class.java,
            File::class.java,
            List::class.java
        )
        method.isAccessible = true
        @Suppress("UNCHECKED_CAST")
        val elements = method.invoke(pathList, listOf(dexFile), null, emptyList<Any>()) as Array<*>
        return elements[0]!!
    }
}