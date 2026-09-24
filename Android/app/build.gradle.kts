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
    // NOTE: applicationId/namespace are a development placeholder. Android/README.md
    // flags the package/application ID as intentionally not frozen yet; choose the
    // real one deliberately before this app is ever distributed outside the team.
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
        versionCode = 1
        versionName = "0.1.0"
    }

    buildTypes {
        release {
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
