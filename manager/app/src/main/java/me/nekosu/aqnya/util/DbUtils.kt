package me.nekosu.aqnya.util

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper

class RootDbHelper(
    context: Context,
) : SQLiteOpenHelper(context, "root_manager.db", null, 1) {
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

data class FmacRule(
    val path: String,
    val statusBits: Long,
)

const val FMAC_BIT_DENY = 0
const val FMAC_BIT_NOT_FOUND = 1

class RuleDbHelper(
    context: Context,
) : SQLiteOpenHelper(context, "fmac_rules.db", null, 1) {
    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL(
            "CREATE TABLE rules (path TEXT PRIMARY KEY, status_bits INTEGER NOT NULL)",
        )
    }

    override fun onUpgrade(
        db: SQLiteDatabase,
        old: Int,
        new: Int,
    ) {
        db.execSQL("DROP TABLE IF EXISTS rules")
        onCreate(db)
    }

    fun getAll(): List<FmacRule> {
        val list = mutableListOf<FmacRule>()
        readableDatabase
            .rawQuery(
                "SELECT path, status_bits FROM rules",
                null,
            ).use { c ->
                while (c.moveToNext()) {
                    list += FmacRule(c.getString(0), c.getLong(1))
                }
            }
        return list
    }

    fun getCount(): Int =
        readableDatabase
            .rawQuery("SELECT COUNT(*) FROM rules", null)
            .use { c -> if (c.moveToFirst()) c.getInt(0) else 0 }

    fun insert(rule: FmacRule) {
        val cv =
            ContentValues().apply {
                put("path", rule.path)
                put("status_bits", rule.statusBits)
            }
        writableDatabase.insertWithOnConflict(
            "rules",
            null,
            cv,
            SQLiteDatabase.CONFLICT_REPLACE,
        )
    }

    fun delete(path: String) {
        writableDatabase.delete("rules", "path = ?", arrayOf(path))
    }

    override fun close() = super.close()
}
