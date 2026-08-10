pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }

    // Tinker patch 插件没有发布 Gradle Plugin Marker，
    // 需要手动指定 Maven 坐标让版本目录能解析。
    resolutionStrategy {
        eachPlugin {
            if (requested.id.id == "com.tencent.tinker.patch") {
                useModule("com.tencent.tinker:tinker-patch-gradle-plugin:${requested.version}")
            }
        }
    }
}
plugins {
    id("org.gradle.toolchains.foojay-resolver-convention") version "1.0.0"
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "nekosu"
include(":app")
