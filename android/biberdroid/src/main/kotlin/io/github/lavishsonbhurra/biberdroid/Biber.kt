package io.github.lavishsonbhurra.biberdroid

import android.content.Context
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.util.concurrent.TimeUnit

/**
 * biber, the bibliography processor for biblatex, for Android apps.
 *
 * The program is `libbiber.so` in the app's `nativeLibraryDir`, a launcher that embeds
 * Perl; the Perl modules it loads in C form sit beside it as `libpl_*.so`. Its Perl tree
 * ships as an asset and is unpacked by [install]. The app must package native libraries
 * uncompressed and extracted (`useLegacyPackaging = true`), because Android only lets an
 * app start programs from `nativeLibraryDir`; see the README.
 *
 * Threading: every function here reads files, and most write them or start a process,
 * so call them off the main thread. They are safe to call from several threads at once:
 * installs are serialised within the process, and with the app's other processes by a
 * file lock beside the tree.
 */
public object Biber {

    /** The biber release this library carries. */
    public const val VERSION: String = "2.21"

    /** The biblatex release it matches (biber and biblatex must be used in matching pairs). */
    public const val BIBLATEX_VERSION: String = "3.21"

    /** The name Tectonic looks for on `PATH` when a document uses biblatex. */
    public const val TECTONIC_TOOL: String = "tectonic-biber"

    private const val LAUNCHER = "libbiber.so"
    private const val ASSETS = "biberdroid"
    private const val TREE = "$ASSETS/perl-tree.tar.xz"
    private const val TREE_VERSION = "$ASSETS/perl-tree.version"
    private const val NOTICES = "$ASSETS/NOTICES.txt"
    private const val MAX_OUTPUT = 1 shl 20

    /** The default timeout of [run]: 2 minutes. */
    public const val DEFAULT_TIMEOUT_MILLIS: Long = 120_000

    /** The shipped tree's SHA-256, read once: assets do not change while the app runs. */
    @Volatile
    private var shippedPayload: String? = null

    /** The launcher in this install's `nativeLibraryDir`. */
    @JvmStatic
    public fun launcher(context: Context): File = File(context.applicationInfo.nativeLibraryDir, LAUNCHER)

    /**
     * Whether the launcher is in this install's `nativeLibraryDir`. It is missing on a
     * device without arm64-v8a, and when the app's native libraries are not extracted at
     * install time (`useLegacyPackaging` off). It does not start biber.
     */
    @JvmStatic
    public fun isSupported(context: Context): Boolean = launcher(context).isFile

    /** The licence notices of everything biber brings along, for an app's licences screen. */
    @JvmStatic
    @Throws(IOException::class)
    public fun notices(context: Context): String =
        context.assets.open(NOTICES).use { it.readBytes().decodeToString() }

    /**
     * The version this library ships: the biber release and the SHA-256 of its Perl tree,
     * read from the library's assets. Installs nothing; for diagnostics and about screens.
     *
     * @throws IOException when the app was packaged without the library's assets.
     */
    @JvmStatic
    @Throws(IOException::class)
    public fun shippedVersion(context: Context): BiberVersion = BiberVersion(VERSION, shippedPayload(context))

    /**
     * The version of the tree installed in [home] (as [BiberVersion.payload]), or null when
     * there is none. Installs nothing and starts nothing.
     */
    @JvmStatic
    @JvmOverloads
    public fun installedVersion(context: Context, home: File = defaultHome(context)): String? =
        Trees.readMarker(home)

    /**
     * Describes the installation in [home] without unpacking anything: its paths, the
     * installed version and whether it is current. Cheap enough to call for every compile,
     * so an app can set up an environment with [BiberInstallation.applyTo] without
     * installing first. It works on a device without biber too ([isSupported] false); the
     * installation is then never current.
     *
     * When an earlier install was killed halfway, this also removes the folder it left
     * behind, unless an install is running in this process or another.
     *
     * @throws IOException when the app was packaged without the library's assets.
     */
    @JvmStatic
    @JvmOverloads
    @Throws(IOException::class)
    public fun installation(context: Context, home: File = defaultHome(context)): BiberInstallation =
        Trees.describe(home, launcher(context), shippedPayload(context))

    /**
     * Unpacks the Perl tree into [home] unless this version is already there, and points
     * `home/bin/tectonic-biber` at this install's launcher. The first call takes a second
     * or two (about 12.5 MB are written); later calls return at once.
     *
     * The tree is written into a fresh folder beside [home], with the version marker last,
     * then swapped into place by renaming, so a process killed at any point leaves either
     * the old tree or none, and the next call starts over and removes what the killed one
     * left behind. If the new tree cannot be moved into place, the old one is put back.
     * `nativeLibraryDir` changes when the app is updated, so every call checks the link.
     * When all is in place, a call only reads the version marker and the link.
     *
     * @throws UnsupportedOperationException when [isSupported] is false.
     * @throws IOException when the tree cannot be written (storage full, say).
     */
    @JvmStatic
    @JvmOverloads
    @Throws(IOException::class)
    public fun install(context: Context, home: File = defaultHome(context)): BiberInstallation {
        val exe = launcher(context)
        if (!exe.isFile) {
            throw UnsupportedOperationException(
                "$LAUNCHER is not in ${exe.parent}: biber needs an arm64-v8a device, and the app must " +
                    "package native libraries with useLegacyPackaging = true",
            )
        }
        return Trees.install(home, exe, shippedPayload(context)) { context.assets.open(TREE) }
    }

    /**
     * A process builder that runs biber with [args] in [workDir], for an app that starts
     * and watches the process itself: to stop it, lower its priority, add variables such as
     * `LD_PRELOAD`, or read its output as it comes. It installs biber first if needed, in
     * the default folder, as [run] does.
     *
     * biber gets a small environment of its own: `PATH=/system/bin`, `HOME` set to
     * [workDir], `TMPDIR` set to the app's cache folder, plus what
     * [BiberInstallation.applyTo] adds, then [environment] on top. Standard error is merged
     * into standard output, and standard input reads from `/dev/null`. All of it can be
     * changed on the returned builder before `start()`.
     *
     * The caller owns the process: read its output until the end (or redirect it), since
     * biber blocks when the pipe is full, and wait for it or destroy it.
     *
     * @throws UnsupportedOperationException when [isSupported] is false.
     * @throws IOException when biber cannot be installed.
     */
    @JvmStatic
    @JvmOverloads
    @Throws(IOException::class)
    public fun processBuilder(
        context: Context,
        workDir: File,
        args: List<String>,
        environment: Map<String, String> = emptyMap(),
    ): ProcessBuilder = Trees.processBuilder(install(context), workDir, args, context.cacheDir, environment)

    /**
     * Runs biber with [args] in [workDir] and waits for it, at most [timeoutMillis].
     * It installs biber first if needed. The process is the one [processBuilder] describes,
     * with the same environment.
     *
     * For example, `Biber.run(context, dir, listOf("main"))` processes `dir/main.bcf`
     * into `dir/main.bbl`, and `listOf("--validate-datamodel", "main")` also checks the
     * `.bib` files against biblatex's data model (warnings in `main.blg`).
     *
     * @throws UnsupportedOperationException when [isSupported] is false.
     * @throws IOException when biber cannot be installed or started.
     * @throws InterruptedException when the calling thread is interrupted; biber is stopped.
     */
    @JvmStatic
    @JvmOverloads
    @Throws(IOException::class, InterruptedException::class)
    public fun run(
        context: Context,
        workDir: File,
        args: List<String>,
        timeoutMillis: Long = DEFAULT_TIMEOUT_MILLIS,
        environment: Map<String, String> = emptyMap(),
    ): BiberResult = waitFor(processBuilder(context, workDir, args, environment).start(), timeoutMillis)

    /** Collects [process]'s output (at most [MAX_OUTPUT] bytes) and waits for it, at most [timeoutMillis]. */
    internal fun waitFor(process: Process, timeoutMillis: Long): BiberResult {
        val output = ByteArrayOutputStream()
        val reader = Thread {
            val buf = ByteArray(8192)
            process.inputStream.use { input ->
                while (true) {
                    val n = try {
                        input.read(buf)
                    } catch (_: IOException) {
                        -1
                    }
                    if (n < 0) break
                    synchronized(output) {
                        if (output.size() < MAX_OUTPUT) output.write(buf, 0, minOf(n, MAX_OUTPUT - output.size()))
                    }
                }
            }
        }
        reader.isDaemon = true
        reader.start()
        try {
            val finished = process.waitFor(timeoutMillis, TimeUnit.MILLISECONDS)
            if (!finished) {
                process.destroyForcibly()
                process.waitFor()
            }
            reader.join(5_000)
            val text = synchronized(output) { output.toString("UTF-8") }
            return BiberResult(if (finished) process.exitValue() else -1, text, !finished)
        } catch (e: InterruptedException) {
            process.destroyForcibly()
            throw e
        }
    }

    private fun defaultHome(context: Context): File = File(context.filesDir, "biber")

    private fun shippedPayload(context: Context): String =
        shippedPayload ?: context.assets.open(TREE_VERSION).use { it.readBytes().decodeToString().trim() }
            .also { shippedPayload = it }
}
