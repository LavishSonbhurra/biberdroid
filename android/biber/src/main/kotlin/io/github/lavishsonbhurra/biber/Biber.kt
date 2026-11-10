package io.github.lavishsonbhurra.biber

import android.content.Context
import android.system.ErrnoException
import android.system.Os
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.util.concurrent.TimeUnit
import org.tukaani.xz.XZInputStream

/**
 * biber, the bibliography processor for biblatex, for Android apps.
 *
 * The program is `libbiber.so` in the app's `nativeLibraryDir`, a launcher that embeds
 * Perl; the Perl modules it loads in C form sit beside it as `libpl_*.so`. Its Perl tree
 * ships as an asset and is unpacked by [install]. The app must package native libraries
 * uncompressed and extracted (`useLegacyPackaging = true`), because Android only lets an
 * app start programs from `nativeLibraryDir`; see the README.
 *
 * Every function here does file or process work: call them off the main thread.
 */
public object Biber {

    /** The biber release this library carries. */
    public const val VERSION: String = "2.21"

    /** The biblatex release it matches (biber and biblatex must be used in matching pairs). */
    public const val BIBLATEX_VERSION: String = "3.21"

    /** The name Tectonic looks for on `PATH` when a document uses biblatex. */
    public const val TECTONIC_TOOL: String = "tectonic-biber"

    private const val LAUNCHER = "libbiber.so"
    private const val ASSETS = "biber-android"
    private const val TREE = "$ASSETS/perl-tree.tar.xz"
    private const val TREE_VERSION = "$ASSETS/perl-tree.version"
    private const val NOTICES = "$ASSETS/NOTICES.txt"
    private const val MARKER = ".version"
    private const val MAX_OUTPUT = 1 shl 20

    /** The default timeout of [run]: 2 minutes. */
    public const val DEFAULT_TIMEOUT_MILLIS: Long = 120_000

    /** Homes whose tree and link were checked in this process. */
    private val verified = HashSet<String>()

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
     * Unpacks the Perl tree into [home] unless this version is already there, and points
     * `home/bin/tectonic-biber` at this install's launcher. The first call takes a second
     * or two (about 12.5 MB are written); later calls return at once.
     *
     * The tree is written into a fresh folder beside [home], with the version marker last,
     * then swapped into place by renaming, so a process killed at any point leaves either
     * the old tree or none, and the next call starts over. If the new tree cannot be moved
     * into place, the old one is put back. `nativeLibraryDir` changes when
     * the app is updated, so the link is checked once per process.
     *
     * @throws UnsupportedOperationException when [isSupported] is false.
     * @throws IOException when the tree cannot be written (storage full, say).
     */
    @JvmStatic
    @JvmOverloads
    @Synchronized
    @Throws(IOException::class)
    public fun install(context: Context, home: File = File(context.filesDir, "biber")): BiberInstallation {
        val exe = launcher(context)
        if (!exe.isFile) {
            throw UnsupportedOperationException(
                "$LAUNCHER is not in ${exe.parent}: biber needs an arm64-v8a device, and the app must " +
                    "package native libraries with useLegacyPackaging = true",
            )
        }
        val nativeDir = exe.parentFile!!
        val key = home.absolutePath
        if (key in verified) readMarker(home)?.let { return BiberInstallation(home, exe, nativeDir, it) }
        val want = context.assets.open(TREE_VERSION).use { it.readBytes().decodeToString().trim() }
        home.absoluteFile.parentFile?.mkdirs()
        // Another process of the same app may be installing too.
        RandomAccessFile(File(home.absoluteFile.parentFile, "${home.name}.lock"), "rw").use { lockFile ->
            lockFile.channel.lock().use {
                if (readMarker(home) != want) unpack(context, home, want)
                try {
                    link(exe, File(home, "bin/$TECTONIC_TOOL"))
                } catch (e: ErrnoException) {
                    throw IOException("Could not link $TECTONIC_TOOL", e)
                }
            }
        }
        verified += key
        return BiberInstallation(home, exe, nativeDir, want)
    }

    /**
     * Runs biber with [args] in [workDir] and waits for it, at most [timeoutMillis].
     * It installs biber first if needed. biber starts with a small environment of its own:
     * `PATH=/system/bin`, `HOME` set to [workDir], `TMPDIR` set to the app's cache folder,
     * plus what [BiberInstallation.applyTo] adds, then [environment] on top.
     *
     * For example, `Biber.run(context, dir, listOf("main"))` processes `dir/main.bcf`
     * into `dir/main.bbl`, and `listOf("--validate-datamodel", "main")` also checks the
     * `.bib` files against biblatex's data model (warnings in `main.blg`).
     *
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
    ): BiberResult {
        val biber = install(context)
        val pb = ProcessBuilder(biber.command(args)).directory(workDir).redirectErrorStream(true)
        val env = pb.environment()
        env.clear()
        env["PATH"] = "/system/bin"
        env["HOME"] = workDir.absolutePath
        env["TMPDIR"] = context.cacheDir.absolutePath
        biber.applyTo(env)
        env.putAll(environment)
        val process = pb.start()
        // biber reads no input; a closed stdin means nothing can wait on it.
        process.outputStream.close()
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

    private fun readMarker(home: File): String? {
        val marker = File(home, MARKER)
        return if (marker.isFile) marker.readText().trim() else null
    }

    private fun unpack(context: Context, home: File, version: String) {
        val parent = home.absoluteFile.parentFile!!
        val fresh = File(parent, "${home.name}.unpacking")
        val old = File(parent, "${home.name}.old")
        fresh.deleteRecursively()
        old.deleteRecursively()
        if (!fresh.mkdirs()) throw IOException("Could not make $fresh")
        try {
            context.assets.open(TREE).use { raw ->
                XZInputStream(raw.buffered()).use { xz ->
                    Tar.read(xz) { entry, body ->
                        val out = File(fresh, entry.name)
                        if (entry.isDirectory) {
                            out.mkdirs()
                        } else {
                            out.parentFile?.mkdirs()
                            out.outputStream().use { body.copyTo(it) }
                        }
                    }
                }
            }
            File(fresh, "bin").mkdirs()
            File(fresh, MARKER).writeText(version)
        } catch (e: IOException) {
            // Storage is usually full here: leave nothing half-written behind.
            fresh.deleteRecursively()
            throw e
        }
        TreeSwap.swap(fresh, home, old)
    }

    /** Points [link] at [target], replacing a link to an older install's launcher by a rename. */
    private fun link(target: File, link: File) {
        val current = try {
            Os.readlink(link.absolutePath)
        } catch (_: ErrnoException) {
            null
        }
        if (current == target.absolutePath) return
        link.parentFile?.mkdirs()
        val tmp = File(link.parentFile, "${link.name}.new")
        tmp.delete()
        Os.symlink(target.absolutePath, tmp.absolutePath)
        Os.rename(tmp.absolutePath, link.absolutePath)
    }
}
