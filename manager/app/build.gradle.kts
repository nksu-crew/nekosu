plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
    kotlin("plugin.serialization") version embeddedKotlinVersion
    alias(libs.plugins.ktlint)
    alias(libs.plugins.tinker.patch)
}

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
            "x86_64"
        )
     }
    }
    
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
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
    implementation(libs.tinker)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.compose.ui.test.junit4)
    debugImplementation(libs.androidx.compose.ui.tooling)
    debugImplementation(libs.androidx.compose.ui.test.manifest)
}

// ── Tinker Patch 生成配置 ──
// 通过 -PoldApk=… -PoldMapping=… -PoldResourceMapping=… 传入旧包路径
tinkerPatch {
    // 旧版本 APK 路径（CI 通过 gradle 属性传入）
    oldApk = project.findProperty("oldApk") as? String
        ?: "${project.rootDir}/tinker-old/old.apk"

    // ProGuard/R8 mapping 文件
    applyMapping = project.findProperty("oldMapping") as? String
        ?: "${project.rootDir}/tinker-old/mapping.txt"

    // R.txt 资源映射文件
    resourceMapping = project.findProperty("oldResourceMapping") as? String
        ?: "${project.rootDir}/tinker-old/R.txt"

    // 忽略警告
    ignoreWarning = true

    // 对生成的补丁签名
    useSign = true

    tinkerId = project.findProperty("tinkerId") as? String
        ?: gitCommitHash()

    buildConfig {
        applyMapping = project.findProperty("oldMapping") as? String
            ?: "${project.rootDir}/tinker-old/mapping.txt"
        applyResourceMapping = project.findProperty("oldResourceMapping") as? String
            ?: "${project.rootDir}/tinker-old/R.txt"
    }

    dex {
        // ART 下使用 raw 模式减少补丁体积
        dexMode = "jar"
        pattern = listOf(
            "classes*.dex",
            "assets/secondary-dex-?.jar",
        )
        loader = listOf(
            "com.tencent.tinker.loader.*",
            "me.nekosu.aqnya.Application",
        )
    }

    lib {
        pattern = listOf("lib/*/*.so")
    }

    res {
        pattern = listOf("res/*", "r/*", "assets/*", "resources.arsc", "AndroidManifest.xml")
        ignoreChange = listOf(
            "assets/sample_meta.txt", // 忽略测试资源
        )
        largeModSize = 100
    }

    packageConfig {
        configField("patchMessage", "tinker patch via CI @ ${gitCommitHash()}")
        configField("platform", "all")
        configField("patchVersion", "1.0")
    }

    sevenZip {
        zipArtifact = "com.tencent.mm:SevenZip:1.2.17"
    }
}