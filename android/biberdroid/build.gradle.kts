// io.github.lavishsonbhurra:biberdroid: biber for Android as an AAR.
//
// The AAR holds the payload ../build.sh makes, which is never committed:
//   jni/arm64-v8a/                    libbiber.so (the launcher), libperl.so, libxml216.so,
//                                     libz1.so, libiconv.so and the libpl_*.so XS modules
//   assets/biberdroid/perl-tree.tar.xz   the Perl tree, unpacked by Biber.install
//   assets/biberdroid/perl-tree.version  its SHA-256, the installed tree's version
//   assets/biberdroid/NOTICES.txt        licences of everything in the payload
// and the Kotlin API in src/main/kotlin.
//
// Name the payload folder (BIBER_WORK/out after ./build.sh package) with the Gradle
// property biber.payload or the environment variable BIBER_PAYLOAD; the default is
// ../work/out from the repository root.
//
// Publishing: ./gradlew :biberdroid:publishReleasePublicationToStagingRepository writes a
// Maven repository to build/central-staging. It is signed when a key is given:
//   signingInMemoryKey, signingInMemoryKeyPassword      (Gradle properties), or
//   SIGNING_KEY, SIGNING_PASSPHRASE                     (environment)
// ../scripts/central-bundle.py and central-upload.py take it from there.
import java.security.MessageDigest

plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
    `maven-publish`
    signing
}

group = "io.github.lavishsonbhurra"
version = providers.gradleProperty("biberdroidVersion").get()

val repoUrl = "https://github.com/LavishSonbhurra/biberdroid"

android {
    namespace = "io.github.lavishsonbhurra.biberdroid"
    compileSdk = 36

    defaultConfig {
        // futimes and lutimes, which Termux's builds call, are in bionic from API 26.
        minSdk = 26
        consumerProguardFiles("consumer-rules.pro")
        ndk { abiFilters += "arm64-v8a" }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        buildConfig = false
        resValues = false
    }

    packaging {
        jniLibs {
            // The payload's ELF files are already stripped and were edited with patchelf;
            // stripping them again gains nothing.
            keepDebugSymbols += "**/*.so"
        }
    }

    publishing {
        singleVariant("release") {
            withSourcesJar()
            withJavadocJar()
        }
    }

    testOptions {
        unitTests.isReturnDefaultValues = true
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
        // Usable from apps on Kotlin 2.1 or later, not only the compiler that built it.
        languageVersion.set(org.jetbrains.kotlin.gradle.dsl.KotlinVersion.KOTLIN_2_1)
        apiVersion.set(org.jetbrains.kotlin.gradle.dsl.KotlinVersion.KOTLIN_2_1)
    }
    coreLibrariesVersion = "2.1.21"
    explicitApi()
}

dependencies {
    // Reads the xz-compressed Perl tree. 0BSD.
    implementation("org.tukaani:xz:1.12")
    testImplementation("junit:junit:4.13.2")
}

// ---- the payload -----------------------------------------------------------------

val payloadDir: Provider<Directory> =
    providers.gradleProperty("biber.payload")
        .orElse(providers.environmentVariable("BIBER_PAYLOAD"))
        // A relative path is taken from where Gradle was started.
        .map { layout.projectDirectory.dir(gradle.startParameter.currentDir.resolve(it).absolutePath) }
        .orElse(rootProject.layout.projectDirectory.dir("../work/out"))

/** Copies the payload's jniLibs, or its Perl tree and notices, into a generated source folder. */
abstract class PayloadTask : DefaultTask() {
    @get:Internal
    abstract val payload: DirectoryProperty

    @get:Input
    abstract val part: Property<String>

    @get:InputFiles
    @get:PathSensitive(PathSensitivity.RELATIVE)
    val inputs: FileCollection
        get() = payload.get().asFileTree.matching {
            if (part.get() == "jniLibs") include("jniLibs/arm64-v8a/*.so")
            else include("perl-tree.tar.xz", "NOTICES.txt")
        }

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun copy() {
        val from = payload.get().asFile
        val out = outputDir.get().asFile
        out.deleteRecursively()
        if (part.get() == "jniLibs") {
            val libs = File(from, "jniLibs/arm64-v8a").listFiles { f -> f.name.endsWith(".so") }.orEmpty()
            require(libs.any { it.name == "libbiber.so" }) {
                "No payload in $from: run ../build.sh, or set biber.payload (BIBER_PAYLOAD) to its out folder"
            }
            val abi = File(out, "arm64-v8a").apply { mkdirs() }
            libs.sorted().forEach { it.copyTo(File(abi, it.name)) }
        } else {
            val tree = File(from, "perl-tree.tar.xz")
            val notices = File(from, "NOTICES.txt")
            require(tree.isFile && notices.isFile) {
                "No perl-tree.tar.xz or NOTICES.txt in $from: run ../build.sh package"
            }
            val dir = File(out, "biberdroid").apply { mkdirs() }
            tree.copyTo(File(dir, "perl-tree.tar.xz"))
            notices.copyTo(File(dir, "NOTICES.txt"))
            val sha = MessageDigest.getInstance("SHA-256").digest(tree.readBytes())
            File(dir, "perl-tree.version").writeText(sha.joinToString("") { "%02x".format(it) })
        }
    }
}

androidComponents {
    onVariants { variant ->
        val name = variant.name.replaceFirstChar { it.uppercase() }
        val jniLibs = tasks.register<PayloadTask>("payloadJniLibs$name") {
            payload.set(payloadDir)
            part.set("jniLibs")
        }
        val assets = tasks.register<PayloadTask>("payloadAssets$name") {
            payload.set(payloadDir)
            part.set("assets")
        }
        variant.sources.jniLibs?.addGeneratedSourceDirectory(jniLibs, PayloadTask::outputDir)
        variant.sources.assets?.addGeneratedSourceDirectory(assets, PayloadTask::outputDir)
    }
}

// ---- publishing --------------------------------------------------------------------

publishing {
    repositories {
        maven {
            name = "staging"
            url = uri(rootProject.layout.buildDirectory.dir("central-staging"))
        }
    }
    publications {
        register<MavenPublication>("release") {
            artifactId = "biberdroid"
            afterEvaluate { from(components["release"]) }
            pom {
                name.set("biberdroid")
                description.set(
                    "biber 2.21, the bibliography processor for biblatex 3.21, built for Android " +
                        "(arm64-v8a, API 26+) with Perl 5.44 and the CPAN modules it needs, " +
                        "and a small API to install and run it.",
                )
                url.set(repoUrl)
                licenses {
                    license {
                        name.set("Artistic-2.0")
                        url.set("https://opensource.org/license/artistic-2-0")
                        comments.set(
                            "This project's code. The bundled biber, Perl, CPAN modules, libxml2, zlib " +
                                "and libiconv keep their own licences, listed in " +
                                "assets/biberdroid/NOTICES.txt in the AAR.",
                        )
                    }
                }
                developers {
                    developer {
                        id.set("LavishSonbhurra")
                        name.set("Lavish Sonbhurra")
                        url.set("https://github.com/LavishSonbhurra")
                    }
                }
                scm {
                    url.set(repoUrl)
                    connection.set("scm:git:$repoUrl.git")
                    developerConnection.set("scm:git:ssh://git@github.com/LavishSonbhurra/biberdroid.git")
                }
            }
        }
    }
}

signing {
    val key = providers.gradleProperty("signingInMemoryKey").orElse(providers.environmentVariable("SIGNING_KEY"))
    val password = providers.gradleProperty("signingInMemoryKeyPassword")
        .orElse(providers.environmentVariable("SIGNING_PASSPHRASE"))
    if (key.isPresent) {
        useInMemoryPgpKeys(key.get(), password.orNull)
        sign(publishing.publications)
    }
}

// TarTest.readsThePayloadTree reads the real Perl tree.
tasks.withType<Test>().configureEach {
    val tree = payloadDir.map { it.file("perl-tree.tar.xz").asFile.path }
    inputs.property("biber.tree", tree)
    doFirst { systemProperty("biber.tree", tree.get()) }
}
