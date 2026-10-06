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

    defaultConfig {
        applicationId = "com.turnhub.android"
        minSdk = 26
        targetSdk = 37
        // Play rejects a versionCode it has already seen. CI passes its run
        // number (-Pturnhub.versionCode); local builds keep 1 unless given one.
        versionCode = (findProperty("turnhub.versionCode") as String?)?.toInt() ?: 1
        versionName = "0.1.0"
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
    }
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
}
