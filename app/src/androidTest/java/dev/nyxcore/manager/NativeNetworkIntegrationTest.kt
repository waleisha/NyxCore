package dev.nyxcore.manager

import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeNetworkIntegrationTest {
    @Test
    fun configuredNetworkDoctorPasses() {
        val args = InstrumentationRegistry.getArguments()
        val url = args.getString("nyxNetworkDoctorUrl")
        assertTrue(
            "native network integration gate failed",
            NativeTestBridge.nativeCheckNetIntegration(url)
        )
    }
}
