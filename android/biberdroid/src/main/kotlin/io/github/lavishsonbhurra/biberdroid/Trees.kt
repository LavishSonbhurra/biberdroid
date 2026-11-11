package io.github.lavishsonbhurra.biberdroid

import java.io.File
import java.io.IOException
import java.io.InputStream
import java.io.RandomAccessFile
import java.nio.channels.OverlappingFileLockException
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.util.concurrent.locks.ReentrantLock
import org.tukaani.xz.XZInputStream

/**
 * The installed Perl tree: reading its state, unpacking it and linking `tectonic-biber`.
 * Nothing here needs a Context, so it runs in JVM tests; [Biber] passes in the paths and
 * the assets.
 *
 * Around [home] (`filesDir/biber` by default) live:
 *   `biber.lock`       the file lock every change to the tree is made under
 *   `biber.unpacking`  a tree being unpacked, renamed to `biber` when complete
 *   `biber.old`        the previous tree while the new one is moved into place
 *   `biber/bin/tectonic-biber.new`  a link being made, renamed over the old one
 * The last three only outlive an install that was killed.
 */
internal object Trees {

    const val MARKER = ".version"

    /**
     * Held while this process takes the file lock. A JVM may hold a file's lock only once,
     * so without it an install would fail, not wait, while a [cleanIfIdle] holds the lock.
     */
    private val inProcess = ReentrantLock()

    private fun parent(home: File): File = home.absoluteFile.parentFile!!

    fun lockFile(home: File): File = File(parent(home), "${home.name}.lock")

    fun freshDir(home: File): File = File(parent(home), "${home.name}.unpacking")

    fun oldDir(home: File): File = File(parent(home), "${home.name}.old")

    fun tool(home: File): File = File(home, "bin/${Biber.TECTONIC_TOOL}")

    private fun toolTmp(home: File): File = File(home, "bin/${Biber.TECTONIC_TOOL}.new")

    /** What an install that was killed can leave behind. */
    fun leftovers(home: File): List<File> = listOf(freshDir(home), oldDir(home), toolTmp(home))

    private fun hasLeftovers(home: File): Boolean =
        leftovers(home).any { it.exists() || Files.isSymbolicLink(it.toPath()) }

    /** The installed tree's version, or null when there is none (or it cannot be read). */
    fun readMarker(home: File): String? {
        val marker = File(home, MARKER)
        return try {
            if (marker.isFile) marker.readText().trim().ifEmpty { null } else null
        } catch (_: IOException) {
            null
        }
    }

    /** Where [link] points, or null when it is not a symlink. */
    fun linkTarget(link: File): String? = try {
        Files.readSymbolicLink(link.toPath()).toString()
    } catch (_: IOException) {
        null
    } catch (_: UnsupportedOperationException) {
        null
    }

    /**
     * Describes the tree at [home] without changing it, apart from removing what a killed
     * install left behind when no install is running (see [cleanIfIdle]).
     */
    fun describe(home: File, launcher: File, shipped: String): BiberInstallation {
        cleanIfIdle(home)
        return snapshot(home, launcher, shipped)
    }

    private fun snapshot(home: File, launcher: File, shipped: String): BiberInstallation {
        val installed = readMarker(home)
        val current = installed == shipped && launcher.isFile && linkTarget(tool(home)) == launcher.absolutePath
        return BiberInstallation(home, launcher, launcher.parentFile ?: launcher, installed, shipped, current)
    }

    /**
     * Removes the [leftovers] of a killed install, but only when no install holds the lock,
     * in this process or another; otherwise it leaves them to that install. Cheap when there
     * is nothing to remove: a few file checks, and no lock file is made.
     */
    fun cleanIfIdle(home: File) {
        if (!hasLeftovers(home) || !inProcess.tryLock()) return
        try {
            RandomAccessFile(lockFile(home), "rw").use { file ->
                val lock = try {
                    file.channel.tryLock()
                } catch (_: OverlappingFileLockException) {
                    null
                } ?: return
                lock.use { removeLeftovers(home) }
            }
        } catch (_: IOException) {
            // Not ours to report: the next install removes them under its lock.
        } finally {
            inProcess.unlock()
        }
    }

    private fun removeLeftovers(home: File) {
        freshDir(home).deleteRecursively()
        oldDir(home).deleteRecursively()
        toolTmp(home).delete()
    }

    /**
     * Unpacks the tree from [openTree] (a `.tar.xz`) into [home] unless version [shipped]
     * is already there, points `home/bin/tectonic-biber` at [launcher], and removes what a
     * killed install left behind. Serialised within the process, and with other processes
     * by a file lock. When all is in place already, it only reads the marker and the link.
     */
    fun install(home: File, launcher: File, shipped: String, openTree: () -> InputStream): BiberInstallation {
        // The marker is written last, so a matching one means a complete tree.
        val before = snapshot(home, launcher, shipped)
        if (before.isCurrent && !hasLeftovers(home)) return before
        inProcess.lock()
        try {
            parent(home).mkdirs()
            RandomAccessFile(lockFile(home), "rw").use { lockFile ->
                lockFile.channel.lock().use {
                    removeLeftovers(home)
                    if (readMarker(home) != shipped) unpack(home, shipped, openTree)
                    link(launcher, tool(home))
                }
            }
        } finally {
            inProcess.unlock()
        }
        return snapshot(home, launcher, shipped)
    }

    private fun unpack(home: File, version: String, openTree: () -> InputStream) {
        val fresh = freshDir(home)
        if (!fresh.mkdirs()) throw IOException("Could not make $fresh")
        try {
            openTree().use { raw ->
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
        TreeSwap.swap(fresh, home, oldDir(home))
    }

    /** Points [link] at [target], replacing a link to an older install's launcher by a rename. */
    private fun link(target: File, link: File) {
        if (linkTarget(link) == target.absolutePath) return
        link.parentFile?.mkdirs()
        val tmp = File(link.parentFile, "${link.name}.new")
        try {
            Files.deleteIfExists(tmp.toPath())
            Files.createSymbolicLink(tmp.toPath(), target.absoluteFile.toPath())
            Files.move(tmp.toPath(), link.toPath(), StandardCopyOption.ATOMIC_MOVE)
        } catch (e: IOException) {
            throw IOException("Could not link ${Biber.TECTONIC_TOOL}", e)
        } catch (e: UnsupportedOperationException) {
            throw IOException("Could not link ${Biber.TECTONIC_TOOL}", e)
        }
    }

    /**
     * The process that runs biber with [args] in [workDir]: the environment holds only
     * `PATH=/system/bin`, `HOME` ([workDir]), `TMPDIR` ([tmpDir]), what
     * [BiberInstallation.applyTo] adds, then [environment]. Output and error are merged
     * and standard input reads from `/dev/null`.
     */
    fun processBuilder(
        biber: BiberInstallation,
        workDir: File,
        args: List<String>,
        tmpDir: File,
        environment: Map<String, String>,
    ): ProcessBuilder {
        val pb = ProcessBuilder(biber.command(args))
            .directory(workDir)
            .redirectErrorStream(true)
            .redirectInput(ProcessBuilder.Redirect.from(File("/dev/null")))
        val env = pb.environment()
        env.clear()
        env["PATH"] = "/system/bin"
        env["HOME"] = workDir.absolutePath
        env["TMPDIR"] = tmpDir.absolutePath
        biber.applyTo(env)
        env.putAll(environment)
        return pb
    }
}
