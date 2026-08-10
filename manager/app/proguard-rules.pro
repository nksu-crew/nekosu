# Add project specific ProGuard rules here.
# You can control the set of applied configuration files using the
# proguardFiles setting in build.gradle.
#
# For more details, see
#   http://developer.android.com/guide/developing/tools/proguard.html

# If your project uses WebView with JS, uncomment the following
# and specify the fully qualified class name to the JavaScript interface
# class:
#-keepclassmembers class fqcn.of.javascript.interface.for.webview {
#   public *;
#}

# Uncomment this to preserve the line number information for
# debugging stack traces.
#-keepattributes SourceFile,LineNumberTable

# If you keep the line number information, uncomment this to
# hide the original source file name.
#-renamesourcefileattribute SourceFile

# Keep OkHttp3 classes
-dontwarn okhttp3.**
-keep class okhttp3.** { *; }
-keep interface okhttp3.** { *; }

-keep class me.nekosu.aqnya.util.GitHubRelease { 
    *; 
}

-keep class me.nekosu.aqnya.ncore { *; }

-keepattributes RuntimeVisibleAnnotations, RuntimeVisibleParameterAnnotations
-keepattributes AnnotationDefault, Signature, InnerClasses, EnclosingMethod

# ── Tinker 热更新 ProGuard 规则 ──

# 保持 Tinker 核心类不被混淆
-keep class com.tencent.tinker.** { *; }
-keep class com.tencent.tinker.loader.** { *; }
-keep class com.tencent.tinker.lib.** { *; }

# 保持 Application 类
-keep class me.nekosu.aqnya.Application { *; }
-keep class * extends android.app.Application { *; }

# 保持 TinkerManager 不被混淆
-keep class me.nekosu.aqnya.util.TinkerManager { *; }
-keep class me.nekosu.aqnya.util.TinkerUpdateChecker { *; }

# Tinker 要求保持注解
-keepattributes *Annotation*

# 保持构造方法 <init>
-keepclasseswithmembers class * {
    public <init>(android.content.Context, android.util.AttributeSet);
}
-keepclasseswithmembers class * {
    public <init>(android.content.Context, android.util.AttributeSet, int);
}

# 保持 Serializable 相关
-keepclassmembers class * implements java.io.Serializable {
    static final long serialVersionUID;
    private static final java.io.ObjectStreamField[] serialPersistentFields;
    private void writeObject(java.io.ObjectOutputStream);
    private void readObject(java.io.ObjectInputStream);
    java.lang.Object writeReplace();
    java.lang.Object readResolve();
}

# Tinker 补丁合成需要保留行号
-keepattributes SourceFile,LineNumberTable

# 重命名源文件以隐藏真实文件名
-renamesourcefileattribute SourceFile
