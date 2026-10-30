// NOTE: no `kotlin-android` plugin. AGP 9.0+ compiles Kotlin itself via
// "built-in Kotlin support"; applying org.jetbrains.kotlin.android alongside
// it fails Gradle sync outright. kotlin.plugin.compose is unaffected and
// still required for the Compose compiler. See Android/README.md.
plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.turnhub.android"
    // applicationId is frozen: the Google Play listing uses it (Android/README.md).
    // compileSdk 37, not 36: the pinned AndroidX/Compose versions in
    // libs.versions.toml (core-ktx 1.19.0, lifecycle 2.11.0, etc.) declare an
    // AAR metadata minimum of compileSdk 37, and Gradle's own AAR
    // compatibility check refused to build against 36 -- this isn't a stylistic
    // bump, the build fails without it.
    compileSdk = 37
    ndkVersion = "29.0.14206865"
    externalNativeBuild {
        ndkBuild { path = file("src/main/cpp/Android.mk") }
    }

    defaultConfig {
        applicationId = "com.turnhub.android"
        minSdk = 26
        targetSdk = 37
        // Play rejects a versionCode it has already seen. CI passes its run
        // number (-Pturnhub.versionCode); local builds keep 1 unless given one.
        versionCode = (findProperty("turnhub.versionCode") as String?)?.toInt() ?: 1
        versionName = "0.1.0"
        manifestPlaceholders["appLabel"] = "@string/app_name"
        manifestPlaceholders["appIcon"] = "@mipmap/ic_launcher"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        testInstrumentationRunnerArguments["class"] = "com.turnhub.android.standalone.NativeCoreDeviceTest"
    }

    // Play upload key for release bundles, read from the environment so the
    // keystore never enters the repository. CI decodes it from GitHub secrets
    // (CONTINUOUS_INTEGRATION.md, "Android release bundle"); without these
    // variables the release bundle is built unsigned.
    val uploadKeystore = System.getenv("TURNHUB_UPLOAD_KEYSTORE")
    if (!uploadKeystore.isNullOrEmpty()) {
        signingConfigs {
            create("upload") {
                storeFile = file(uploadKeystore)
                storePassword = System.getenv("TURNHUB_UPLOAD_STORE_PASSWORD")
                keyAlias = System.getenv("TURNHUB_UPLOAD_KEY_ALIAS")
                keyPassword = System.getenv("TURNHUB_UPLOAD_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        debug {
            // Opt-in USB preview can coexist with the Play-signed installation.
            if (providers.gradleProperty("turnhub.preview").orNull == "true") {
                applicationIdSuffix = ".preview"
                versionNameSuffix = "-preview"
                manifestPlaceholders["appLabel"] = "TurnHub Preview"
                // Mardi Gras purple and green behind the gold gear: told apart from the Play app (graphite).
                manifestPlaceholders["appIcon"] = "@mipmap/ic_launcher_preview"
            }
        }
        release {
            if (!uploadKeystore.isNullOrEmpty()) {
                signingConfig = signingConfigs.getByName("upload")
            }
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // No kotlinOptions {} block: that DSL came from the kotlin-android plugin,
    // which this project no longer applies (see the plugins block above).
    // Built-in Kotlin support derives jvmTarget from compileOptions above.

    buildFeatures {
        compose = true
        // BuildConfig.DEBUG keeps Death May Die out of release (Play) builds.
        buildConfig = true
    }
    sourceSets.getByName("androidTest").assets.srcDir(rootProject.file("../core/scenarios"))
}

// Run JVM behavior tests through the very same JNI bridge on the build host.
val hostWindows = System.getProperty("os.name").startsWith("Windows")
val hostCoreLibrary = layout.buildDirectory.file("native-host/" + System.mapLibraryName("turnhub_game"))
val hostGameCore = tasks.register<Exec>("buildHostGameCore") {
    val core = rootProject.file("../core")
    val bridge = file("src/main/cpp/game_jni.cpp")
    val hostScript = file("src/main/cpp/build-host.cmd")
    val output = hostCoreLibrary.get().asFile
    inputs.files(fileTree(core.resolve("src")), fileTree(core.resolve("include")), bridge, hostScript)
    outputs.file(hostCoreLibrary)
    doFirst { output.parentFile.mkdirs() }
    val jdk = file(System.getProperty("java.home"))
    if (hostWindows) commandLine("cmd", "/c", hostScript.absolutePath, output.parentFile.absolutePath)
    else commandLine(listOf(providers.environmentVariable("CXX").orNull ?: "g++", "-std=c++17", "-shared",
        "-Wall", "-Wextra", "-Werror", "-fPIC", "-static-libgcc", "-static-libstdc++",
        "-I${jdk.resolve("include")}", "-I${jdk.resolve("include/linux")}",
        "-I${core.resolve("include")}", bridge.absolutePath,
        core.resolve("src/turnhub_core.cpp").absolutePath, core.resolve("src/turnhub_json.cpp").absolutePath,
        "-o", hostCoreLibrary.get().asFile.absolutePath))
}
tasks.withType<Test>().configureEach {
    dependsOn(hostGameCore)
    inputs.file(hostCoreLibrary)
    inputs.dir(rootProject.file("../core/scenarios"))
    systemProperty("turnhub.core.library", hostCoreLibrary.get().asFile.absolutePath)
    systemProperty("turnhub.core.reference", providers.gradleProperty("turnhub.referenceRules").orNull == "true")
    jvmArgs("--enable-native-access=ALL-UNNAMED")
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.activity.compose)
    implementation(libs.kotlinx.coroutines.core)
    implementation(libs.androidx.camera.camera2)
    implementation(libs.androidx.camera.lifecycle)
    implementation(libs.androidx.camera.view)
    implementation(libs.zxing.core)

    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    debugImplementation(libs.androidx.compose.ui.tooling)

    testImplementation(libs.junit)
    testImplementation(libs.kotlinx.coroutines.test)
    testImplementation(libs.org.json)
    androidTestImplementation("androidx.test:runner:1.7.0")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
}
