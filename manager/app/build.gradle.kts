plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
    kotlin("plugin.serialization") version embeddedKotlinVersion
    alias(libs.plugins.ktlint)
}

val omvllFile = file("src/main/lib/omvll-ndk.so")

fun gitCommitCount(): Int =
    ProcessBuilder("git", "rev-list", "--count", "HEAD")
        .directory(rootDir)
        .start()
        .inputStream
        .bufferedReader()
        .readText()
        .trim()
        .toInt()

fun gitCommitHash(): String =
    ProcessBuilder("git", "rev-parse", "--short", "HEAD")
        .directory(rootDir)
        .start()
        .inputStream
        .bufferedReader()
        .readText()
        .trim()

android {
    namespace = "me.nekosu.aqnya"
    compileSdk {
        version =
            release(37) {
                minorApiLevel = 1
            }
    }

    defaultConfig {
        applicationId = "me.nekosu.aqnya"
        minSdk = 27
        targetSdk = 37
        versionCode = gitCommitCount()
        versionName = gitCommitHash()

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        
        ndk {
        abiFilters += listOf(
            "arm64-v8a",
        )
     }
    }
    
    externalNativeBuild {
    cmake {
        path = file("src/main/cpp/CMakeLists.txt")
        cppFlags("-fpass-plugin=${omvllFile.absolutePath}")
        cFlags("-fpass-plugin=${omvllFile.absolutePath}")
      }
    }

    signingConfigs {
        create("debugKey") {
            storeFile = file("$rootDir/debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
        create("releaseKey") {
            storeFile = file("$rootDir/release.keystore")
            storePassword = System.getenv("KEYSTORE_PASSWORD")
            keyAlias = System.getenv("KEY_ALIAS")
            keyPassword = System.getenv("KEY_PASSWORD")
        }
    }

    buildTypes {
        debug {
            signingConfig = signingConfigs.getByName("debugKey")
        }
        release {
            val withR8 = (project.findProperty("withR8") as? String)?.toBoolean() ?: true
            signingConfig = signingConfigs.getByName("releaseKey")
            isMinifyEnabled = withR8
            isShrinkResources = withR8
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.navigation.compose)
    implementation(libs.androidx.compose.material.icons.extended)
    implementation(libs.androidx.datastore.preferences)
    implementation(libs.okhttp)
    implementation(libs.kotlinx.serialization.json)
    implementation(libs.kotlinx.serialization.protobuf)
    implementation(libs.haze)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.compose.ui.test.junit4)
    debugImplementation(libs.androidx.compose.ui.tooling)
    debugImplementation(libs.androidx.compose.ui.test.manifest)
}