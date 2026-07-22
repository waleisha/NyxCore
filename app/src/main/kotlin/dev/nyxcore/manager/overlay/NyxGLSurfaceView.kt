package dev.nyxcore.manager.overlay

import android.app.Activity
import android.app.ActivityManager
import android.content.Context
import android.content.pm.ConfigurationInfo
import android.graphics.Color
import android.graphics.PixelFormat
import android.opengl.GLES30
import android.opengl.GLSurfaceView
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.util.AttributeSet
import android.util.DisplayMetrics
import android.util.Log
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.view.inputmethod.InputConnectionWrapper
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.FrameLayout
import dev.nyxcore.manager.NativeBridge
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

// 透明 GLSurfaceView 叠加层：渲染 native UI，并为 ImGui 窗口铺透明触摸代理。
class NyxGLSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : GLSurfaceView(context, attrs) {
    private val renderer = NyxRenderer()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val windowManager = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager
    private val proxyViews = mutableListOf<View?>()
    private val syncBounds = ProxyBounds()
    private var attachedRoot: ViewGroup? = null
    private var overlayParams: WindowManager.LayoutParams? = null
    private var isPanelAttached = false
    private var hiddenInput: EditText? = null
    private var syncActive = false

    private val regionSyncLoop = object : Runnable {
        override fun run() {
            syncProxyWindows()
            if (syncActive) {
                mainHandler.postDelayed(this, REGION_SYNC_MS)
            }
        }
    }

    init {
        isFocusable = false
        isFocusableInTouchMode = false
        setEGLContextClientVersion(3)
        setEGLConfigChooser(8, 8, 8, 8, 16, 0)
        holder.setFormat(PixelFormat.TRANSLUCENT)
        setZOrderOnTop(true)
        preserveEGLContextOnPause = true
        setRenderer(renderer)
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    fun attachToWindow(root: ViewGroup, reason: String) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            mainHandler.post {
                attachToWindow(root, reason)
            }
            return
        }

        if (!supportsOpenGLES3(context)) {
            Log.w(TAG, "GLSurfaceView panel attach skipped without OpenGL ES 3: reason=$reason")
            return
        }

        attachedRoot = root
        if (isPanelAttached) {
            return
        }

        // Window token 可能晚于 decor 创建，拿不到时延后重试。
        val activity = context as? Activity
        val decor = activity?.window?.decorView
        val token = decor?.windowToken ?: root.windowToken
        if (token == null) {
            (decor ?: root).post {
                if (attachedRoot === root && !isPanelAttached) {
                    attachToWindow(root, reason)
                }
            }
            return
        }

        val params = overlayWindowParams().apply {
            this.token = token
        }
        try {
            windowManager.addView(this, params)
            overlayParams = params
            isPanelAttached = true
            current = this
            val inputRoot = activity?.findViewById<ViewGroup>(android.R.id.content) ?: root
            initHiddenInput(inputRoot)
            startRegionSync()
            Log.i(TAG, "GLSurfaceView panel attached: reason=$reason")
        } catch (error: RuntimeException) {
            Log.w(TAG, "GLSurfaceView panel attach failed: reason=$reason", error)
        }
    }

    fun detachFromWindow(reason: String) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            mainHandler.post {
                detachFromWindow(reason)
            }
            return
        }

        stopRegionSync()
        clearProxyWindows()
        hideKeyboardOnMain()
        removeHiddenInput()

        if (isPanelAttached) {
            try {
                windowManager.removeViewImmediate(this)
            } catch (error: RuntimeException) {
                Log.w(TAG, "GLSurfaceView panel remove failed: reason=$reason", error)
            }
        }

        isPanelAttached = false
        overlayParams = null
        attachedRoot = null
        if (current === this) {
            current = null
        }
        Log.i(TAG, "GLSurfaceView panel detached: reason=$reason")
    }

    fun shutdown() {
        NativeBridge.shutdown()
        detachFromWindow("shutdown")
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        return false
    }

    private fun overlayWindowParams(): WindowManager.LayoutParams {
        val flags = WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM or
            WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS or
            WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
            WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE

        val metrics = DisplayMetrics()
        @Suppress("DEPRECATION")
        windowManager.defaultDisplay.getRealMetrics(metrics)

        return WindowManager.LayoutParams(
            metrics.widthPixels,
            metrics.heightPixels,
            WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
            flags,
            PixelFormat.TRANSLUCENT,
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = 0
            y = 0
        }
    }

    private fun proxyWindowParams(bounds: ProxyBounds): WindowManager.LayoutParams {
        val flags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL or
            WindowManager.LayoutParams.FLAG_SPLIT_TOUCH or
            WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or
            WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS or
            WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM

        return WindowManager.LayoutParams(
            bounds.width,
            bounds.height,
            overlayParams?.type ?: WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
            flags,
            PixelFormat.TRANSLUCENT,
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            token = overlayParams?.token
            x = bounds.left
            y = bounds.top
        }
    }

    private fun startRegionSync() {
        if (syncActive) {
            return
        }

        syncActive = true
        mainHandler.removeCallbacks(regionSyncLoop)
        mainHandler.post(regionSyncLoop)
    }

    private fun stopRegionSync() {
        syncActive = false
        mainHandler.removeCallbacks(regionSyncLoop)
    }

    private fun syncProxyWindows() {
        if (!isPanelAttached) {
            return
        }

        val bounds = imguiWindowBounds()
        if (bounds.size < WINDOW_BOUNDS_STRIDE) {
            hideProxyWindows(0)
            return
        }

        var validIndex = 0
        val windowCount = minOf(bounds.size / WINDOW_BOUNDS_STRIDE, MAX_PROXY_WINDOWS)
        for (index in 0 until windowCount) {
            if (!bounds.readProxyBounds(index, syncBounds)) {
                continue
            }

            val proxy = proxyWindowAt(validIndex, syncBounds)
            if (proxy != null) {
                updateProxy(proxy, syncBounds)
                validIndex++
            }
        }

        hideProxyWindows(validIndex)
    }

    private fun FloatArray.readProxyBounds(index: Int, out: ProxyBounds): Boolean {
        val offset = index * WINDOW_BOUNDS_STRIDE
        val rawLeft = this[offset + WINDOW_BOUNDS_LEFT]
        val rawTop = this[offset + WINDOW_BOUNDS_TOP]
        val rawRight = this[offset + WINDOW_BOUNDS_RIGHT]
        val rawBottom = this[offset + WINDOW_BOUNDS_BOTTOM]
        val rawWidth = rawRight - rawLeft
        val rawHeight = rawBottom - rawTop
        // native 每 4 个 float 返回一个窗口矩形：left/top/right/bottom；0,0 空矩形代表未初始化。
        if (rawWidth <= 1f || rawHeight <= 1f || (rawLeft == 0f && rawTop == 0f)) {
            return false
        }

        val left = (rawLeft - PROXY_PADDING_PX).toInt()
        val top = (rawTop - PROXY_PADDING_PX).toInt()
        val right = (rawRight + PROXY_PADDING_PX).toInt()
        val bottom = (rawBottom + PROXY_PADDING_PX).toInt()
        out.set(left, top, right - left, bottom - top)
        return true
    }

    private fun imguiWindowBounds(): FloatArray {
        return try {
            NativeBridge.nativeGetImGuiWindowBounds()
        } catch (_: UnsatisfiedLinkError) {
            FloatArray(0)
        }
    }

    private fun proxyWindowAt(index: Int, bounds: ProxyBounds): View? {
        while (proxyViews.size <= index) {
            proxyViews.add(null)
        }

        proxyViews[index]?.let {
            return it
        }

        val proxy = InputProxyView(context)
        return try {
            windowManager.addView(proxy, proxyWindowParams(bounds))
            proxyViews[index] = proxy
            proxy
        } catch (error: RuntimeException) {
            Log.w(TAG, "proxy window add failed", error)
            null
        }
    }

    private fun updateProxy(proxy: View, bounds: ProxyBounds) {
        if (proxy.visibility != View.VISIBLE) {
            proxy.visibility = View.VISIBLE
        }

        val params = proxy.layoutParams as? WindowManager.LayoutParams
            ?: proxyWindowParams(bounds)
        if (!params.updateBounds(bounds)) {
            return
        }

        try {
            windowManager.updateViewLayout(proxy, params)
        } catch (error: RuntimeException) {
            Log.w(TAG, "proxy window update failed", error)
        }
    }

    private fun WindowManager.LayoutParams.updateBounds(bounds: ProxyBounds): Boolean {
        if (x == bounds.left && y == bounds.top && width == bounds.width && height == bounds.height) {
            return false
        }

        x = bounds.left
        y = bounds.top
        width = bounds.width
        height = bounds.height
        return true
    }

    private fun hideProxyWindows(start: Int) {
        for (index in start until proxyViews.size) {
            val proxy = proxyViews[index]
            if (proxy != null && proxy.visibility != View.GONE) {
                proxy.visibility = View.GONE
            }
        }
    }

    private fun clearProxyWindows() {
        for (index in proxyViews.indices.reversed()) {
            val proxy = proxyViews[index] ?: continue
            try {
                windowManager.removeViewImmediate(proxy)
            } catch (_: RuntimeException) {
            }
        }
        proxyViews.clear()
    }

    private fun initHiddenInput(root: ViewGroup) {
        if (hiddenInput != null) {
            return
        }

        val editText = createHiddenInput()
        hiddenInput = editText
        val params = if (root is FrameLayout) {
            FrameLayout.LayoutParams(1, 1).apply {
                gravity = Gravity.TOP or Gravity.START
            }
        } else {
            ViewGroup.LayoutParams(1, 1)
        }
        root.addView(editText, params)
    }

    private fun removeHiddenInput() {
        val editText = hiddenInput ?: return
        (editText.parent as? ViewGroup)?.removeView(editText)
        hiddenInput = null
    }

    private fun createHiddenInput(): EditText {
        return object : EditText(context) {
            // 输入法关闭前先让 native 处理特殊按键。
            override fun onKeyPreIme(keyCode: Int, event: KeyEvent): Boolean {
                if (routeInputKey(event)) {
                    return true
                }
                return super.onKeyPreIme(keyCode, event)
            }

            // 部分设备会走这个入口分发输入法按键。
            override fun dispatchKeyEventPreIme(event: KeyEvent): Boolean {
                if (routeInputKey(event)) {
                    return true
                }
                return super.dispatchKeyEventPreIme(event)
            }

            // 包装输入连接，拦截软键盘退格。
            override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection? {
                val base = super.onCreateInputConnection(outAttrs) ?: return null
                return object : InputConnectionWrapper(base, true) {
                    override fun sendKeyEvent(event: KeyEvent): Boolean {
                        if (routeInputKey(event)) {
                            return true
                        }
                        return super.sendKeyEvent(event)
                    }

                    // 部分输入法用 deleteSurroundingText 表示退格。
                    override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
                        repeat(beforeLength) {
                            NativeBridge.nativeOnBackspace()
                        }
                        return true
                    }
                }
            }
        }.apply {
            visibility = View.VISIBLE
            alpha = 0.001f
            setBackgroundColor(Color.TRANSPARENT)
            setTextColor(Color.TRANSPARENT)
            isFocusable = true
            isFocusableInTouchMode = true
            inputType = InputType.TYPE_CLASS_TEXT
            imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI
            addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit

                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                    if (s == null || count <= 0) {
                        return
                    }

                    NativeBridge.nativeOnTextInput(s.subSequence(start, start + count).toString())
                }

                override fun afterTextChanged(s: Editable?) = Unit
            })
        }
    }

    private fun routeInputKey(event: KeyEvent): Boolean {
        if (event.action != KeyEvent.ACTION_DOWN) {
            return false
        }

        return when (event.keyCode) {
            KeyEvent.KEYCODE_DEL -> {
                NativeBridge.nativeOnBackspace()
                true
            }
            else -> false
        }
    }

    private fun showKeyboard() {
        mainHandler.post {
            if (current === this) {
                showKeyboardOnMain()
            }
        }
    }

    private fun hideKeyboard() {
        mainHandler.post {
            if (current === this) {
                hideKeyboardOnMain()
            }
        }
    }

    private fun showKeyboardOnMain() {
        val editText = hiddenInput ?: return
        editText.requestFocus()
        inputMethod().showSoftInput(editText, InputMethodManager.SHOW_IMPLICIT)
    }

    private fun hideKeyboardOnMain() {
        val editText = hiddenInput ?: return
        editText.setText("")
        editText.clearFocus()
        inputMethod().hideSoftInputFromWindow(editText.windowToken, 0)
    }

    private fun inputMethod(): InputMethodManager {
        return context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
    }

    private inner class NyxRenderer : Renderer {
        private var drewFirstFrame = false

        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            drewFirstFrame = false
            NativeBridge.nativeOnSurfaceCreated(holder.surface, gl, config)
            GLES30.glClearColor(0f, 0f, 0f, 0f)
            Log.i(TAG, "surface created")
        }

        override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
            NativeBridge.nativeOnSurfaceChanged(gl, width, height)
            GLES30.glViewport(0, 0, width, height)
            Log.i(TAG, "surface changed: ${width}x$height")
        }

        override fun onDrawFrame(gl: GL10?) {
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT or GLES30.GL_DEPTH_BUFFER_BIT)
            NativeBridge.nativeOnDrawFrame(gl)

            if (!drewFirstFrame) {
                drewFirstFrame = true
                Log.i(TAG, "first frame drawn")
            }
        }
    }

    private class ProxyBounds {
        var left = 0
            private set
        var top = 0
            private set
        var width = 0
            private set
        var height = 0
            private set

        fun set(left: Int, top: Int, width: Int, height: Int) {
            this.left = left
            this.top = top
            this.width = width
            this.height = height
        }
    }

    companion object {
        private const val TAG = "NyxGLSurfaceView"
        private const val REGION_SYNC_MS = 16L
        private const val MAX_PROXY_WINDOWS = 32
        private const val WINDOW_BOUNDS_STRIDE = 4
        private const val WINDOW_BOUNDS_LEFT = 0
        private const val WINDOW_BOUNDS_TOP = 1
        private const val WINDOW_BOUNDS_RIGHT = 2
        private const val WINDOW_BOUNDS_BOTTOM = 3
        // 触摸代理比 ImGui 窗口略大，避免边缘漏触。
        private const val PROXY_PADDING_PX = 20

        @Volatile
        private var current: NyxGLSurfaceView? = null

        @JvmStatic
        fun showInputUI() {
            current?.showKeyboard()
        }

        @JvmStatic
        fun hideInputUI() {
            current?.hideKeyboard()
        }

        private fun supportsOpenGLES3(context: Context): Boolean {
            val activityManager = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
            val info: ConfigurationInfo = activityManager.deviceConfigurationInfo
            return info.reqGlEsVersion >= 0x30000
        }
    }

    private class InputProxyView(context: Context) : View(context) {
        init {
            setBackgroundColor(Color.TRANSPARENT)
        }

        override fun onTouchEvent(event: MotionEvent): Boolean {
            NativeBridge.nativeOnTouch(event.rawX, event.rawY, event.actionMasked)
            return true
        }
    }
}
