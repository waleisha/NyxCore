package dev.nyxcore.manager

import android.os.Bundle
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.activity.ComponentActivity
import dev.nyxcore.manager.overlay.NyxGLSurfaceView

class MainActivity : ComponentActivity() {
    private var glSurfaceView: NyxGLSurfaceView? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        NativeBridge.bindMainThread()

        val root = FrameLayout(this).apply {
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT,
            )
        }
        setContentView(root)

        glSurfaceView = NyxGLSurfaceView(this)
        root.post {
            glSurfaceView?.attachToWindow(root, "activity create")
        }
    }

    override fun onResume() {
        super.onResume()
        glSurfaceView?.onResume()
    }

    override fun onPause() {
        glSurfaceView?.onPause()
        super.onPause()
    }

    override fun onDestroy() {
        glSurfaceView?.shutdown()
        glSurfaceView = null
        super.onDestroy()
    }

    private companion object {
        init {
            System.loadLibrary(BuildConfig.NYX_NATIVE_LIBRARY)
        }
    }
}
