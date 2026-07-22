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

# Native methods are registered dynamically from C++ by class and method name.
-keep class dev.nyxcore.manager.NativeBridge {
    native <methods>;
}

# JNI constructs this DTO directly through the 8-argument Kotlin data-class constructor.
-keep class dev.nyxcore.manager.AuthResult {
    *;
}

# C++ calls these static methods from the render thread to show/hide IME.
-keep class dev.nyxcore.manager.overlay.NyxGLSurfaceView {
    public static void showInputUI();
    public static void hideInputUI();
}
