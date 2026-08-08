package me.nekosu.aqnya.util

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper

class RootDbHelper(
    context: Context,
) : SQLiteOpenHelper(context, "db", null, 1) {
    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL("CREATE TABLE root_apps (packageName TEXT PRIMARY KEY, allowed INTEGER)")
    }

    override fun onUpgrade(
        db: SQLiteDatabase,
        oldVersion: Int,
        newVersion: Int,
    ) {
        db.execSQL("DROP TABLE IF EXISTS root_apps")
        onCreate(db)
    }

    fun getAllowedPackages(): Set<String> {
        val set = mutableSetOf<String>()
        readableDatabase
            .rawQuery(
                "SELECT packageName FROM root_apps WHERE allowed = 1",
                null,
            ).use { c ->
                while (c.moveToNext()) set.add(c.getString(0))
            }
        return set
    }

    fun getAllowedCount(): Int =
        readableDatabase
            .rawQuery(
                "SELECT COUNT(*) FROM root_apps WHERE allowed = 1",
                null,
            ).use { c -> if (c.moveToFirst()) c.getInt(0) else 0 }

    fun setAllowed(
        packageName: String,
        allowed: Boolean,
    ) {
        val cv =
            ContentValues().apply {
                put("packageName", packageName)
                put("allowed", if (allowed) 1 else 0)
            }
        writableDatabase.insertWithOnConflict(
            "root_apps",
            null,
            cv,
            SQLiteDatabase.CONFLICT_REPLACE,
        )
    }

    fun isAllowed(packageName: String): Boolean =
        readableDatabase
            .rawQuery(
                "SELECT 1 FROM root_apps WHERE packageName = ? AND allowed = 1 LIMIT 1",
                arrayOf(packageName),
            ).use { c ->
                c.moveToFirst()
            }

    override fun close() = super.close()
}
