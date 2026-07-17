package me.nekosu.aqnya

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import android.util.Log
import java.io.File
import java.nio.ByteBuffer
import java.security.KeyFactory
import java.security.KeyStore
import java.security.spec.PKCS8EncodedKeySpec
import java.util.Locale
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.Mac
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec
import kotlin.experimental.and

object KeyUtils {
    private const val KEY_FILE_NAME = "ecc_private.enc"
    private const val KEYSTORE_ALIAS = "aqnya_ecc_wrap_key"
    private const val ANDROID_KEYSTORE = "AndroidKeyStore"
    private const val AES_GCM_TRANSFORMATION = "AES/GCM/NoPadding"
    private const val GCM_TAG_LENGTH = 128
    private const val GCM_IV_LENGTH = 12
    private const val MAX_KEY_SIZE = 4 * 1024

    private fun ensureWrappingKey() {
        val ks = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        if (ks.containsAlias(KEYSTORE_ALIAS)) return

        val keyGen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE)
        keyGen.init(
            KeyGenParameterSpec
                .Builder(
                    KEYSTORE_ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
                ).setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .setUserAuthenticationRequired(false)
                .build(),
        )
        keyGen.generateKey()
    }

    private fun getWrappingKey(): javax.crypto.SecretKey {
        val ks = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        return (ks.getEntry(KEYSTORE_ALIAS, null) as KeyStore.SecretKeyEntry).secretKey
    }

    fun checkKeyExists(context: Context): Boolean = File(context.filesDir, KEY_FILE_NAME).exists()

    fun getKeyFilePath(context: Context): String = File(context.filesDir, KEY_FILE_NAME).absolutePath

    fun saveKey(
        context: Context,
        key: String,
    ) {
        try {
            val keyBytes = key.toByteArray(Charsets.UTF_8)
            if (keyBytes.size > MAX_KEY_SIZE) return

            ensureWrappingKey()
            val cipher = Cipher.getInstance(AES_GCM_TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, getWrappingKey())

            val iv = cipher.iv
            val encrypted = cipher.doFinal(keyBytes)

            val output = ByteArray(GCM_IV_LENGTH + encrypted.size)
            System.arraycopy(iv, 0, output, 0, GCM_IV_LENGTH)
            System.arraycopy(encrypted, 0, output, GCM_IV_LENGTH, encrypted.size)

            File(context.filesDir, KEY_FILE_NAME).writeBytes(output)
        } catch (e: Exception) {
            Log.e("KeyUtils", "saveKey failed: ${e.message}")
        }
    }

    fun loadKey(context: Context): String? {
        return try {
            val file = File(context.filesDir, KEY_FILE_NAME)
            if (!file.exists()) return null

            val data = file.readBytes()
            if (data.size <= GCM_IV_LENGTH) return null

            val iv = data.copyOfRange(0, GCM_IV_LENGTH)
            val ciphertext = data.copyOfRange(GCM_IV_LENGTH, data.size)

            val cipher = Cipher.getInstance(AES_GCM_TRANSFORMATION)
            cipher.init(
                Cipher.DECRYPT_MODE,
                getWrappingKey(),
                GCMParameterSpec(GCM_TAG_LENGTH, iv),
            )
            val plaintext = cipher.doFinal(ciphertext)
            val result = String(plaintext, Charsets.UTF_8)
            plaintext.fill(0) // 清零中间缓冲
            result
        } catch (e: Exception) {
            Log.e("KeyUtils", "loadKey failed: ${e.message}")
            null
        }
    }

    fun deleteKey(context: Context) {
        File(context.filesDir, KEY_FILE_NAME).delete()
        runCatching {
            val ks = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
            ks.deleteEntry(KEYSTORE_ALIAS)
        }
    }

    fun getTotpToken(
        secretBase32: String,
        timeInterval: Long = 30L,
    ): String {
        try {
            val secretBytes = decodeBase32(secretBase32)
            val time = System.currentTimeMillis() / 1000 / timeInterval
            val data = ByteBuffer.allocate(8).putLong(time).array()

            val algo = "HmacSHA1"
            val mac = Mac.getInstance(algo)
            mac.init(SecretKeySpec(secretBytes, algo))
            val hash = mac.doFinal(data)

            val offset = (hash[hash.size - 1] and 0xf.toByte()).toInt()
            val binary =
                ((hash[offset] and 0x7f.toByte()).toInt() shl 24) or
                    ((hash[offset + 1] and 0xff.toByte()).toInt() shl 16) or
                    ((hash[offset + 2] and 0xff.toByte()).toInt() shl 8) or
                    (hash[offset + 3] and 0xff.toByte()).toInt()

            return String.format("%06d", binary % 1_000_000)
        } catch (e: Exception) {
            Log.e("KeyUtils", "TOTP Generation failed: ${e.message}")
            return ""
        }
    }

    private fun decodeBase32(base32: String): ByteArray {
        val base32Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"
        val noPadding =
            base32
                .trim()
                .replace(" ", "")
                .replace("-", "")
                .uppercase(Locale.ROOT)
                .trimEnd('=')

        val bytes = ArrayList<Byte>()
        var buffer = 0
        var bitsLeft = 0

        for (char in noPadding) {
            val value = base32Chars.indexOf(char)
            if (value < 0) throw IllegalArgumentException("Invalid Base32 character: $char")
            buffer = (buffer shl 5) or value
            bitsLeft += 5
            if (bitsLeft >= 8) {
                bitsLeft -= 8
                bytes.add((buffer shr bitsLeft).toByte())
            }
        }
        return bytes.toByteArray()
    }

    fun isValidECCKey(keyString: String): Boolean {
        if (keyString.isBlank()) return false

        val cleanKey =
            keyString
                .lines()
                .map { it.trim() }
                .filter { it.isNotEmpty() && !it.startsWith("-----") }
                .joinToString("")

        if (cleanKey.isEmpty()) return false

        return try {
            val keyBytes = Base64.decode(cleanKey, Base64.DEFAULT)
            val factory = KeyFactory.getInstance("EC", "AndroidOpenSSL")

            try {
                factory.generatePrivate(PKCS8EncodedKeySpec(keyBytes))
                return true
            } catch (_: Exception) {
            }

            try {
                factory.generatePrivate(PKCS8EncodedKeySpec(sec1ToPkcs8(keyBytes)))
                return true
            } catch (_: Exception) {
            }

            Log.e("KeyUtils", "No valid private key!")
            false
        } catch (e: Exception) {
            Log.e("KeyUtils", "isValidECCKey error: $e")
            false
        }
    }

    private fun sec1ToPkcs8(sec1Der: ByteArray): ByteArray {
        // AlgorithmIdentifier for EC / prime256v1
        // SEQUENCE { OID 1.2.840.10045.2.1, OID 1.2.840.10045.3.1.7 }
        val algorithmIdentifier =
            byteArrayOf(
                0x30,
                0x13,
                0x06,
                0x07,
                0x2a,
                0x86.toByte(),
                0x48,
                0xce.toByte(),
                0x3d,
                0x02,
                0x01,
                0x06,
                0x08,
                0x2a,
                0x86.toByte(),
                0x48,
                0xce.toByte(),
                0x3d,
                0x03,
                0x01,
                0x07,
            )

        // OCTET STRING wrapping sec1
        val octetString = derTlv(0x04, sec1Der)
        val version = byteArrayOf(0x02, 0x01, 0x00)
        val inner = version + algorithmIdentifier + octetString

        return derTlv(0x30, inner)
    }

    private fun derTlv(
        tag: Int,
        value: ByteArray,
    ): ByteArray {
        val len = value.size
        val lenBytes =
            when {
                len < 0x80 -> {
                    byteArrayOf(len.toByte())
                }

                len < 0x100 -> {
                    byteArrayOf(0x81.toByte(), len.toByte())
                }

                else -> {
                    byteArrayOf(
                        0x82.toByte(),
                        (len shr 8).toByte(),
                        (len and 0xff).toByte(),
                    )
                }
            }
        return byteArrayOf(tag.toByte()) + lenBytes + value
    }

    fun loadKeyBytes(context: Context): ByteArray? {
        return try {
            val file = File(context.filesDir, KEY_FILE_NAME)
            if (!file.exists()) return null
            val data = file.readBytes()
            if (data.size <= GCM_IV_LENGTH) return null

            val iv = data.copyOfRange(0, GCM_IV_LENGTH)
            val ciphertext = data.copyOfRange(GCM_IV_LENGTH, data.size)

            val cipher = Cipher.getInstance(AES_GCM_TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, getWrappingKey(), GCMParameterSpec(GCM_TAG_LENGTH, iv))
            cipher.doFinal(ciphertext)
        } catch (e: Exception) {
            Log.e("KeyUtils", "loadKeyBytes failed: ${e.message}")
            null
        }
    }
}
