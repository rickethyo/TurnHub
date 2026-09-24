// Top-level build file. Per-module configuration lives in app/build.gradle.kts;
// this file only declares which plugin versions are available to subprojects.
//
// NOTE: no `kotlin-android` plugin here. AGP 9.0+ (we're on 9.4.0) compiles
// Kotlin itself via "built-in Kotlin support", so the separate
// org.jetbrains.kotlin.android plugin is obsolete and actively conflicts with
// it (confirmed by a real Gradle sync failure -- see Android/README.md).
// org.jetbrains.kotlin.plugin.compose is still required for the Compose
// compiler and is unaffected by that change.
plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.kotlin.compose) apply false
}
