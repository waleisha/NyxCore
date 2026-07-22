import com.github.megatronking.stringfog.plugin.StringFogExtension
import com.github.megatronking.stringfog.plugin.StringFogMode
import com.github.megatronking.stringfog.plugin.kg.RandomKeyGenerator
import java.io.File
import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.compose.compiler)
}

apply(plugin = "stringfog")

data class AppConfig(
    val namespace: String,
    val applicationId: String,
    val compileSdk: Int,
    val minSdk: Int,
    val targetSdk: Int,
    val versionCode: Int,
    val versionName: String,
    val javaTarget: JavaVersion,
    val kotlinJvmTarget: String,
)

data class NativeConfig(
    val ndkVersion: String,
    val cxxStandardFlag: String,
    val supportedAbis: List<String>,
)

data class ModConfig(
    val id: String,
    val mode: String,
    val dir: File,
    val propertiesFile: File,
    val outputName: String,
    val publishEnabled: Boolean,
) {
    val cmakeArgs: List<String>
        get() = listOf("-DNYX_TARGET_TAG=$id")
}

val appConfig = AppConfig(
    namespace = "dev.nyxcore.manager",
    applicationId = "dev.nyxcore.manager",
    compileSdk = 36,
    minSdk = 24,
    targetSdk = 36,
    versionCode = 1,
    versionName = "1.0",
    javaTarget = JavaVersion.VERSION_11,
    kotlinJvmTarget = "11",
)

val nativeConfig = NativeConfig(
    ndkVersion = "30.0.14904198-beta1",
    cxxStandardFlag = "-std=c++20",
    supportedAbis = listOf("arm64-v8a", "armeabi-v7a"),
)

val stringFogImpl = "com.github.megatronking.stringfog.xor.StringFogImpl"
val buildModes = setOf("debug", "release")
val booleanPropertyValues = mapOf(
    "true" to true,
    "on" to true,
    "yes" to true,
    "1" to true,
    "false" to false,
    "off" to false,
    "no" to false,
    "0" to false,
)
val nativeNameRegex = Regex("[A-Za-z0-9_]+")

data class ReleaseSigning(
    val storeFile: File,
    val storePassword: String,
    val keyAlias: String,
    val keyPassword: String,
)

fun nonBlank(value: String?): String? = value?.trim()?.takeIf { it.isNotEmpty() }

fun readProperties(file: File): Properties {
    if (!file.isFile) {
        throw GradleException("Properties file missing: ${file.path}")
    }

    val properties = Properties()
    file.inputStream().use { properties.load(it) }
    return properties
}

fun readOptionalProperties(file: File): Properties {
    return if (file.isFile) readProperties(file) else Properties()
}

val localPropertiesFile = rootProject.file("local.properties")
val localProperties by lazy { readOptionalProperties(localPropertiesFile) }
val publicSigningPropertiesFile = rootProject.file("keystores/public-signing.properties")
val publicSigningProperties by lazy { readOptionalProperties(publicSigningPropertiesFile) }

fun localProperty(name: String): String? {
    return localProperties
        .getProperty(name)
        .let(::nonBlank)
}

fun requiredProperty(properties: Properties, name: String, source: File): String {
    return nonBlank(properties.getProperty(name))
        ?: throw GradleException("Required property '$name' is empty in ${source.path}")
}

fun optionalProperty(properties: Properties, name: String, defaultValue: String = ""): String {
    return nonBlank(properties.getProperty(name)) ?: defaultValue
}

fun boolProperty(properties: Properties, name: String, source: File, defaultValue: String = "false"): Boolean {
    val value = optionalProperty(properties, name, defaultValue).lowercase()
    return booleanPropertyValues[value]
        ?: throw GradleException("Invalid boolean property '$name=$value' in ${source.path}")
}

fun releaseSigningProperty(name: String, envName: String): String? {
    val gradleName = "nyx.release.signing.$name"
    return nonBlank(providers.gradleProperty(gradleName).orNull)
        ?: nonBlank(localProperty(gradleName))
        ?: nonBlank(providers.environmentVariable(envName).orNull)
}

fun rootRelativeFile(path: String): File {
    val file = File(path)
    return if (file.isAbsolute) file else rootProject.file(path)
}

fun requireSigningStoreFile(file: File, source: String): File {
    if (!file.isFile) {
        throw GradleException("$source signing store file does not exist: ${file.path}")
    }
    return file
}

fun publicSigningProperty(name: String): String? {
    return nonBlank(publicSigningProperties.getProperty(name))
}

fun readPublicSigning(): ReleaseSigning? {
    if (!publicSigningPropertiesFile.isFile) {
        return null
    }

    val properties = mapOf(
        "storeFile" to publicSigningProperty("storeFile"),
        "storePassword" to publicSigningProperty("storePassword"),
        "keyAlias" to publicSigningProperty("keyAlias"),
        "keyPassword" to publicSigningProperty("keyPassword"),
    )
    val missing = properties.filterValues { it == null }.keys
    if (missing.isNotEmpty()) {
        throw GradleException(
            "Public signing config is incomplete. Missing: " +
                missing.joinToString()
        )
    }

    val storeFilePath = properties.getValue("storeFile")!!
    val storeFile = requireSigningStoreFile(rootRelativeFile(storeFilePath), "Public")

    return ReleaseSigning(
        storeFile = storeFile,
        storePassword = properties.getValue("storePassword")!!,
        keyAlias = properties.getValue("keyAlias")!!,
        keyPassword = properties.getValue("keyPassword")!!,
    )
}

fun readReleaseSigning(): ReleaseSigning? {
    val properties = mapOf(
        "storeFile" to releaseSigningProperty("storeFile", "NYX_RELEASE_STORE_FILE"),
        "storePassword" to releaseSigningProperty("storePassword", "NYX_RELEASE_STORE_PASSWORD"),
        "keyAlias" to releaseSigningProperty("keyAlias", "NYX_RELEASE_KEY_ALIAS"),
        "keyPassword" to releaseSigningProperty("keyPassword", "NYX_RELEASE_KEY_PASSWORD"),
    )

    if (properties.values.all { it == null }) {
        return readPublicSigning()
    }

    val missing = properties.filterValues { it == null }.keys
    if (missing.isNotEmpty()) {
        throw GradleException(
            "Release signing is partially configured. Missing: " +
                missing.joinToString { "nyx.release.signing.$it" }
        )
    }

    val storeFilePath = properties.getValue("storeFile")!!
    val storeFile = requireSigningStoreFile(rootRelativeFile(storeFilePath), "Release")

    return ReleaseSigning(
        storeFile = storeFile,
        storePassword = properties.getValue("storePassword")!!,
        keyAlias = properties.getValue("keyAlias")!!,
        keyPassword = properties.getValue("keyPassword")!!,
    )
}

fun quotedBuildConfig(value: String): String {
    return "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"") + "\""
}

fun validateNativeName(value: String, name: String, source: File) {
    if (!nativeNameRegex.matches(value)) {
        throw GradleException("$name must contain only letters, digits, or underscores in ${source.path}: $value")
    }
}

fun readActiveMod(nativeRoot: File): ModConfig {
    val activePropertiesFile = rootProject.file("config/active.properties")
    val activeProperties = readProperties(activePropertiesFile)
    val selectedMod = nonBlank(providers.gradleProperty("nyxMod").orNull)
        ?: requiredProperty(activeProperties, "mod", activePropertiesFile)
    val mode = optionalProperty(activeProperties, "mode", "debug").lowercase()
    if (mode !in buildModes) {
        throw GradleException("mode must be debug or release in ${activePropertiesFile.path}: $mode")
    }
    validateNativeName(selectedMod, "mod", activePropertiesFile)

    val modDir = nativeRoot.resolve("mods/$selectedMod")
    val propertiesFile = modDir.resolve("mod.properties")
    val properties = readProperties(propertiesFile)
    val modId = requiredProperty(properties, "id", propertiesFile)
    if (modId != selectedMod) {
        throw GradleException("mod id '$modId' does not match active mod '$selectedMod'")
    }

    val outputName = requiredProperty(properties, "outputName", propertiesFile)
    validateNativeName(outputName, "outputName", propertiesFile)

    return ModConfig(
        id = selectedMod,
        mode = mode,
        dir = modDir,
        propertiesFile = propertiesFile,
        outputName = outputName,
        publishEnabled = boolProperty(properties, "publish", propertiesFile, "true"),
    )
}

fun isReleaseTaskRequested(): Boolean {
    return gradle.startParameter.taskNames.any { taskName ->
        val task = taskName.substringAfterLast(':')
        task.equals("build", ignoreCase = true) ||
            task.equals("assemble", ignoreCase = true) ||
            task.equals("bundle", ignoreCase = true) ||
            task.contains("Release", ignoreCase = true)
    }
}

val debugNativeArgs = listOf(
    "-DNYX_ENABLE_NATIVE_TESTS=ON",
    "-DNYX_ENABLE_INTEGRATION_GATES=ON",
    "-DNYX_ENABLE_BENCHMARKS=ON",
    "-DNYX_ENABLE_NATIVE_LOGS=ON",
)

val releaseNativeArgs = listOf(
    "-DNYX_ENABLE_NATIVE_TESTS=OFF",
    "-DNYX_ENABLE_INTEGRATION_GATES=OFF",
    "-DNYX_ENABLE_BENCHMARKS=OFF",
    "-DNYX_ENABLE_NATIVE_LOGS=OFF",
)

val configuredNdkPath = nonBlank(providers.gradleProperty("nyxNdkPath").orNull)
    ?: localProperty("nyx.ndk.dir")
    ?: localProperty("ndk.dir")
    ?: nonBlank(providers.environmentVariable("ANDROID_NDK_HOME").orNull)
    ?: nonBlank(providers.environmentVariable("ANDROID_NDK_ROOT").orNull)
val configuredNdkDir = configuredNdkPath?.let { File(it).absoluteFile }
if (configuredNdkDir != null && !configuredNdkDir.isDirectory) {
    throw GradleException(
        "Configured NDK path does not exist: ${configuredNdkDir.path}. " +
            "Set -PnyxNdkPath, local.properties nyx.ndk.dir, ANDROID_NDK_HOME, or ANDROID_NDK_ROOT."
    )
}

val nativeRoot = project.file("src/main/cpp")
val activeMod = readActiveMod(nativeRoot)
val releaseTaskRequested = isReleaseTaskRequested()
if (!activeMod.publishEnabled && releaseTaskRequested) {
    throw GradleException("Active mod '${activeMod.id}' is marked publish=false in ${activeMod.propertiesFile.path}")
}

val releaseSigning = readReleaseSigning()

tasks.register("nyxInfo") {
    group = "nyx"
    description = "Print the active NyxCore mod and build inputs."

    doLast {
        println("NyxCore active mod")
        println("  mod: ${activeMod.id}")
        println("  output: lib${activeMod.outputName}.so")
        println("  publish: ${activeMod.publishEnabled}")
        println("  mode label: ${activeMod.mode}")
        println("  mod dir: ${activeMod.dir.relativeTo(rootProject.projectDir).path}")
        println("  ndk: ${configuredNdkDir?.path ?: "Android Gradle Plugin default"}")
        println()
        println("One-off switch:")
        println("  .\\gradlew.bat :app:assembleDebug -PnyxMod=<mod>")
    }
}

tasks.register("nyxMods") {
    group = "nyx"
    description = "List NyxCore mods that can be selected with -PnyxMod."

    doLast {
        val modDirs = nativeRoot.resolve("mods")
            .listFiles { file -> file.isDirectory && file.resolve("mod.properties").isFile }
            ?.sortedBy { it.name }
            .orEmpty()

        if (modDirs.isEmpty()) {
            println("No mods found under ${nativeRoot.resolve("mods").path}")
            return@doLast
        }

        println("NyxCore mods")
        modDirs.forEach { modDir ->
            val source = modDir.resolve("mod.properties")
            val properties = readProperties(source)
            val id = optionalProperty(properties, "id", modDir.name)
            val output = optionalProperty(properties, "outputName", id)
            val publish = optionalProperty(properties, "publish", "true")
            val auth = optionalProperty(properties, "auth", "none")
            val marker = if (id == activeMod.id) "*" else " "
            println("$marker $id -> lib$output.so, publish=$publish, auth=$auth")
        }
    }
}

configure<StringFogExtension> {
    enable = true
    implementation = stringFogImpl
    packageName = appConfig.applicationId
    fogPackages = arrayOf(appConfig.namespace)
    kg = RandomKeyGenerator()
    mode = StringFogMode.bytes
}

android {
    namespace = appConfig.namespace
    compileSdk = appConfig.compileSdk
    ndkVersion = nativeConfig.ndkVersion
    configuredNdkDir?.let { ndkPath = it.path }

    buildFeatures {
        buildConfig = true
        compose = true
    }

    defaultConfig {
        applicationId = appConfig.applicationId
        minSdk = appConfig.minSdk
        targetSdk = appConfig.targetSdk
        versionCode = appConfig.versionCode
        versionName = appConfig.versionName

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        buildConfigField("String", "NYX_ACTIVE_MOD", quotedBuildConfig(activeMod.id))
        buildConfigField("String", "NYX_ACTIVE_MODE", quotedBuildConfig(activeMod.mode))
        buildConfigField("String", "NYX_NATIVE_LIBRARY", quotedBuildConfig(activeMod.outputName))

        ndk {
            nativeConfig.supportedAbis.forEach(abiFilters::add)
        }

        externalNativeBuild {
            cmake {
                arguments += activeMod.cmakeArgs
                cppFlags += nativeConfig.cxxStandardFlag
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    sourceSets {
        getByName("main") {
            java.srcDir("src/main/kotlin")
            assets.srcDir(activeMod.dir.resolve("assets"))
        }
    }

    signingConfigs {
        releaseSigning?.let { credentials ->
            create("release") {
                storeFile = credentials.storeFile
                storePassword = credentials.storePassword
                keyAlias = credentials.keyAlias
                keyPassword = credentials.keyPassword
            }
        }
    }

    buildTypes {
        debug {
            externalNativeBuild {
                cmake {
                    arguments += debugNativeArgs
                }
            }
        }

        release {
            // AGP 8.x runs R8 when code shrinking is enabled for app builds.
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            releaseSigning?.let {
                signingConfig = signingConfigs.getByName("release")
            }
            externalNativeBuild {
                cmake {
                    arguments += releaseNativeArgs
                }
            }
        }
    }

    compileOptions {
        sourceCompatibility = appConfig.javaTarget
        targetCompatibility = appConfig.javaTarget
    }

    kotlinOptions {
        jvmTarget = appConfig.kotlinJvmTarget
    }
}

dependencies {
    implementation(platform(libs.androidx.compose.bom))
    implementation("com.github.megatronking.stringfog:xor:5.0.0")
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.extended)

    testImplementation(libs.junit)

    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.test.runner)
    androidTestImplementation(libs.androidx.compose.ui.test.junit4)

    debugImplementation(libs.androidx.compose.ui.tooling)
    debugImplementation(libs.androidx.compose.ui.test.manifest)
}
