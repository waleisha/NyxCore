package dev.nyxcore.manager

import android.content.Context

object NativeTestBridge {
    init {
        System.loadLibrary(BuildConfig.NYX_NATIVE_LIBRARY)
    }

    external fun nativeCheckRelease(): Boolean
    external fun nativeCheckNetIntegration(url: String?): Boolean
    external fun nativeCheckAuthIntegration(license: String, varKey: String?): Boolean
    external fun nativeCheckAuthIntegrationWithContext(
        context: Context,
        license: String,
        varKey: String?,
    ): Boolean
}
