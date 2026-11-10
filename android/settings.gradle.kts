// The Android library that packages the biber payload (../build.sh) as an AAR.
// It is a build of its own, apart from the payload recipe: run Gradle from this
// folder, for example ./gradlew :biberdroid:assembleRelease.
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "biberdroid"
include(":biberdroid")
