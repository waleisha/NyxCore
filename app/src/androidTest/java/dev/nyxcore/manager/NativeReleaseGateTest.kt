package dev.nyxcore.manager

import org.junit.Assert.assertTrue
import org.junit.Test

class NativeReleaseGateTest {
    @Test
    fun nativeReleaseGatePasses() {
        assertTrue("native release gate failed", NativeTestBridge.nativeCheckRelease())
    }
}
