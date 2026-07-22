package dev.nyxcore.manager

import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeFalse
import org.junit.Test

class NativeAuthIntegrationTest {
    @Test
    fun configuredAuthGatePasses() {
        val args = InstrumentationRegistry.getArguments()
        val license = args.getString("nyxAuthLicense").orEmpty()
        val varKey = args.getString("nyxAuthVarKey")

        assumeFalse("nyxAuthLicense instrumentation arg is required", license.isBlank())

        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(
            "native auth integration gate failed",
            NativeTestBridge.nativeCheckAuthIntegrationWithContext(
                context,
                license,
                varKey,
            )
        )
    }
}
