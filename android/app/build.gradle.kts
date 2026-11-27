plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Version de l'appli : comparée aux releases GitHub (tag « vX.Y »).
// Version : celle de la release GitHub (-PappVersion=3.7 dans le workflow), sinon celle-ci.
val appVersion = (findProperty("appVersion") as String?)?.removePrefix("v") ?: "3.6"
// 3.6 → 30600, 3.6.2 → 30602 (toujours croissant).
val appVersionCode = appVersion.split('.').map { it.filter(Char::isDigit).toIntOrNull() ?: 0 }
    .let { (it.getOrElse(0) { 0 } * 10000) + (it.getOrElse(1) { 0 } * 100) + it.getOrElse(2) { 0 } }

android {
    namespace = "fr.popov.homerun"
    compileSdk = 36

    defaultConfig {
        applicationId = "fr.popov.homerun"
        minSdk = 21
        targetSdk = 35
        versionCode = appVersionCode
        versionName = appVersion
    }

    signingConfigs {
        // Clé de signature fixe (versionnée) : un APK compilé localement ou par
        // GitHub Actions peut remplacer le précédent sans désinstaller.
        create("shared") {
            storeFile = file("../debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }

    buildTypes {
        getByName("debug") {
            signingConfig = signingConfigs.getByName("shared")
        }
        getByName("release") {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("shared")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        buildConfig = true
    }
    androidResources {
        @Suppress("UnstableApiUsage")
        generateLocaleConfig = true
    }
    packaging {
        jniLibs { useLegacyPackaging = true }
    }
}

dependencies {
    implementation("org.videolan.android:libvlc-all:3.7.8")
    implementation("androidx.appcompat:appcompat:1.7.1")
    implementation("androidx.recyclerview:recyclerview:1.4.0")
    implementation("androidx.preference:preference-ktx:1.2.1")
    implementation("androidx.core:core-ktx:1.16.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.9.4")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")
}
