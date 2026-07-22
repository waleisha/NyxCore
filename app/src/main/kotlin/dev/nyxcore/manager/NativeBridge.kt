package dev.nyxcore.manager

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.Surface
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

data class AuthResult(
    val operation: String = "idle",
    val success: Boolean = false,
    val code: Int = 0,
    val failure: Int = 0,
    val message: String = "",
    val value: String = "",
    val configured: Boolean = false,
    val hasSession: Boolean = false,
)

object NativeBridge {
    init {
        System.loadLibrary(BuildConfig.NYX_NATIVE_LIBRARY)
    }

    private val mainHandler = Handler(Looper.getMainLooper())
    private val authResultClass = AuthResult::class.java
    // native 认证状态共享会话，所有调用保持串行并在主线程回调 UI。
    private val authExecutorLock = Any()
    private var authExecutor = createAuthExecutor()

    fun bindMainThread() {
        nativeBindMainThread()
    }

    fun configureAuth(context: Context, callback: (AuthResult) -> Unit) {
        val app = context.applicationContext
        runAuthCall(callback) {
            nativeConfigureAuth(app, authResultClass)
        }
    }

    fun authStatus(callback: (AuthResult) -> Unit) {
        runAuthCall(callback) {
            nativeAuthStatus(authResultClass)
        }
    }

    fun loginAuth(license: String, callback: (AuthResult) -> Unit) {
        runAuthCall(callback) {
            nativeLoginAuth(license, authResultClass)
        }
    }

    fun logoutAuth(callback: (AuthResult) -> Unit) {
        runAuthCall(callback) {
            nativeLogoutAuth(authResultClass)
        }
    }

    fun canRunAuth(feature: String, callback: (AuthResult) -> Unit) {
        runAuthCall(callback) {
            nativeCanRunAuth(feature, authResultClass)
        }
    }

    fun canRunRuntime(callback: (AuthResult) -> Unit) {
        runAuthCall(callback) {
            nativeCanRunRuntime(authResultClass)
        }
    }

    fun fetchAuthVar(key: String, callback: (AuthResult) -> Unit) {
        runAuthCall(callback) {
            nativeFetchAuthVar(key, authResultClass)
        }
    }

    fun fetchAuthNotice(callback: (AuthResult) -> Unit) {
        runAuthCall(callback) {
            nativeFetchAuthNotice(authResultClass)
        }
    }

    fun fetchAuthUpdate(callback: (AuthResult) -> Unit) {
        runAuthCall(callback) {
            nativeFetchAuthUpdate(authResultClass)
        }
    }

    fun installPayloadSeedFromAuth(callback: (AuthResult) -> Unit) {
        runAuthCall(callback) {
            nativeInstallPayloadSeedFromAuth(authResultClass)
        }
    }

    fun shutdown() {
        nativeShutdown()
        shutdownAuthExecutor()
    }

    external fun nativeOnSurfaceCreated(surface: Surface, gl: GL10?, config: EGLConfig?)
    external fun nativeOnSurfaceChanged(gl: GL10?, width: Int, height: Int)
    external fun nativeOnDrawFrame(gl: GL10?)
    external fun nativeOnTouch(x: Float, y: Float, action: Int): Boolean
    external fun nativeOnTextInput(text: String)
    external fun nativeOnBackspace()
    external fun nativeGetImGuiWindowBounds(): FloatArray
    external fun nativeOnSurfaceDestroyed(surface: Surface)

    private external fun nativeBindMainThread()
    private external fun nativeShutdown()
    private external fun nativeConfigureAuth(context: Context, resultClass: Class<AuthResult>): Any?
    private external fun nativeAuthStatus(resultClass: Class<AuthResult>): Any?
    private external fun nativeLoginAuth(license: String, resultClass: Class<AuthResult>): Any?
    private external fun nativeLogoutAuth(resultClass: Class<AuthResult>): Any?
    private external fun nativeCanRunAuth(feature: String, resultClass: Class<AuthResult>): Any?
    private external fun nativeCanRunRuntime(resultClass: Class<AuthResult>): Any?
    private external fun nativeFetchAuthVar(key: String, resultClass: Class<AuthResult>): Any?
    private external fun nativeFetchAuthNotice(resultClass: Class<AuthResult>): Any?
    private external fun nativeFetchAuthUpdate(resultClass: Class<AuthResult>): Any?
    private external fun nativeInstallPayloadSeedFromAuth(resultClass: Class<AuthResult>): Any?

    private fun createAuthExecutor(): ExecutorService {
        return Executors.newSingleThreadExecutor { task ->
            Thread(task, "nyx-auth").apply {
                isDaemon = true
            }
        }
    }

    private fun shutdownAuthExecutor() {
        synchronized(authExecutorLock) {
            authExecutor.shutdownNow()
        }
    }

    private fun runAuthCall(callback: (AuthResult) -> Unit, call: () -> Any?) {
        val executor = synchronized(authExecutorLock) {
            if (authExecutor.isShutdown) {
                authExecutor = createAuthExecutor()
            }
            authExecutor
        }

        executor.execute {
            val result = try {
                call() as? AuthResult ?: authFailure("native auth result missing")
            } catch (_: Throwable) {
                authFailure("native auth call failed")
            }
            mainHandler.post {
                callback(result)
            }
        }
    }

    private fun authFailure(message: String): AuthResult {
        return AuthResult(
            operation = "auth",
            failure = AUTH_FAILURE_NATIVE,
            message = message,
        )
    }

    private const val AUTH_FAILURE_NATIVE = 5
}
