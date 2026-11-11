package io.github.lavishsonbhurra.biberdroid

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.tukaani.xz.LZMA2Options
import org.tukaani.xz.XZOutputStream

class TreesTest {

    private val root: File = Files.createTempDirectory("trees").toFile()
    private val home = File(root, "files/biber")
    private val nativeDir = File(root, "lib/arm64").apply { mkdirs() }
    private val launcher = File(nativeDir, "libbiber.so").apply { writeText("launcher") }

    /** A .tar.xz holding bin/biber with [text], as scripts/archive.py packs the tree. */
    private fun tree(text: String): ByteArray {
        val tar = ByteArrayOutputStream()
        fun header(name: String, type: Char, size: Int) {
            val h = ByteArray(512)
            fun put(off: Int, s: String) = s.toByteArray().copyInto(h, off)
            put(0, name)
            put(100, "0000644\u0000")
            put(124, "%011o\u0000".format(size))
            h[156] = type.code.toByte()
            put(257, "ustar\u0000")
            put(263, "00")
            for (i in 148 until 156) h[i] = ' '.code.toByte()
            put(148, "%06o\u0000 ".format(h.sumOf { it.toInt() and 0xff }))
            tar.write(h)
        }
        val body = text.toByteArray()
        header("bin/", '5', 0)
        header("bin/biber", '0', body.size)
        tar.write(body)
        tar.write(ByteArray((512 - body.size % 512) % 512))
        tar.write(ByteArray(1024))
        val xz = ByteArrayOutputStream()
        XZOutputStream(xz, LZMA2Options()).use { it.write(tar.toByteArray()) }
        return xz.toByteArray()
    }

    private fun install(version: String, archive: ByteArray = tree("biber $version")) =
        Trees.install(home, launcher, version) { ByteArrayInputStream(archive) }

    private fun leave(vararg files: File) {
        for (f in files) {
            f.parentFile!!.mkdirs()
            if (f.name.endsWith(".new")) Files.createSymbolicLink(f.toPath(), launcher.toPath()) else File(f, "lib").mkdirs()
        }
    }

    @Test
    fun describesNothingInstalledWithoutTouchingTheDisk() {
        val biber = Trees.describe(home, launcher, "v1")
        assertFalse(biber.isInstalled)
        assertFalse(biber.isCurrent)
        assertNull(biber.installedVersion)
        assertEquals(home, biber.home)
        assertEquals(launcher, biber.launcher)
        assertEquals(nativeDir, biber.nativeLibraryDir)
        assertFalse(home.parentFile!!.exists())
    }

    @Test
    fun installUnpacksLinksAndIsCurrent() {
        val biber = install("v1")
        assertTrue(biber.isCurrent)
        assertEquals("v1", biber.installedVersion)
        assertEquals("biber v1", File(home, "bin/biber").readText())
        assertEquals(launcher.absolutePath, Files.readSymbolicLink(biber.tectonicTool.toPath()).toString())
        val again = Trees.describe(home, launcher, "v1")
        assertTrue(again.isInstalled)
        assertTrue(again.isCurrent)
        assertEquals("v1", Trees.readMarker(home))
    }

    @Test
    fun anOlderTreeIsInstalledButNotCurrent() {
        install("v1")
        val biber = Trees.describe(home, launcher, "v2")
        assertTrue(biber.isInstalled)
        assertFalse(biber.isCurrent)
        assertEquals("v1", biber.installedVersion)
        @Suppress("DEPRECATION")
        assertEquals("v1", biber.version)
        assertEquals("v2", install("v2").installedVersion)
        assertEquals("biber v2", File(home, "bin/biber").readText())
        assertFalse(Trees.oldDir(home).exists())
    }

    @Test
    fun theDeprecatedVersionIsTheShippedOneBeforeAnInstall() {
        @Suppress("DEPRECATION")
        assertEquals("v1", Trees.describe(home, launcher, "v1").version)
    }

    @Test
    fun aNewLauncherIsRelinked() {
        install("v1")
        val moved = File(root, "lib2/libbiber.so").apply { parentFile!!.mkdirs(); writeText("launcher") }
        assertFalse(Trees.describe(home, moved, "v1").isCurrent)
        val biber = Trees.install(home, moved, "v1") { throw AssertionError("unpacked again") }
        assertTrue(biber.isCurrent)
        assertEquals(moved.absolutePath, Trees.linkTarget(biber.tectonicTool))
    }

    @Test
    fun aCurrentInstallOpensNothing() {
        install("v1")
        Trees.install(home, launcher, "v1") { throw AssertionError("opened the archive") }
    }

    @Test
    fun noLauncherIsNeverCurrent() {
        install("v1")
        launcher.delete()
        assertFalse(Trees.describe(home, launcher, "v1").isCurrent)
    }

    @Test
    fun installRemovesWhatAKilledInstallLeft() {
        install("v1")
        val left = Trees.leftovers(home)
        leave(*left.toTypedArray())
        assertEquals("v1", install("v1").installedVersion)
        for (f in left) assertFalse("$f", f.exists() || Files.isSymbolicLink(f.toPath()))
    }

    @Test
    fun aKilledFirstInstallIsCleanedUpAndRedone() {
        // Killed while unpacking: no tree yet, a partial one beside it.
        leave(Trees.freshDir(home))
        assertFalse(Trees.describe(home, launcher, "v1").isInstalled)
        assertFalse(Trees.freshDir(home).exists())
        leave(Trees.freshDir(home))
        assertTrue(install("v1").isCurrent)
        assertFalse(Trees.freshDir(home).exists())
    }

    @Test
    fun describeRemovesLeftoversWhenNoInstallRuns() {
        install("v1")
        val left = Trees.leftovers(home)
        leave(*left.toTypedArray())
        assertTrue(Trees.describe(home, launcher, "v1").isCurrent)
        for (f in left) assertFalse("$f", f.exists() || Files.isSymbolicLink(f.toPath()))
    }

    @Test
    fun describeLeavesLeftoversToARunningInstall() {
        install("v1")
        leave(Trees.freshDir(home), Trees.oldDir(home))
        RandomAccessFile(Trees.lockFile(home), "rw").use { file ->
            file.channel.lock().use {
                Trees.describe(home, launcher, "v1")
                assertTrue(Trees.freshDir(home).exists())
                assertTrue(Trees.oldDir(home).exists())
            }
        }
        Trees.describe(home, launcher, "v1")
        assertFalse(Trees.freshDir(home).exists())
        assertFalse(Trees.oldDir(home).exists())
    }

    @Test
    fun describeNeitherWaitsForNorDisturbsAnInstallInThisProcess() {
        val opened = CountDownLatch(1)
        val proceed = CountDownLatch(1)
        val archive = tree("biber v1")
        var failure: Throwable? = null
        val installer = thread {
            try {
                Trees.install(home, launcher, "v1") {
                    opened.countDown()
                    proceed.await(10, TimeUnit.SECONDS)
                    ByteArrayInputStream(archive)
                }
            } catch (t: Throwable) {
                failure = t
            }
        }
        assertTrue(opened.await(10, TimeUnit.SECONDS))
        val started = System.nanoTime()
        val during = Trees.describe(home, launcher, "v1")
        assertTrue(System.nanoTime() - started < TimeUnit.SECONDS.toNanos(2))
        assertFalse(during.isInstalled)
        assertTrue("the unpacking folder was removed under the install", Trees.freshDir(home).isDirectory)
        proceed.countDown()
        installer.join(10_000)
        failure?.let { throw it }
        assertTrue(Trees.describe(home, launcher, "v1").isCurrent)
    }

    @Test
    fun describeMakesNoLockFileWhenThereIsNothingToClean() {
        install("v1")
        Trees.lockFile(home).delete()
        Trees.describe(home, launcher, "v1")
        assertFalse(Trees.lockFile(home).exists())
    }

    @Test
    fun aDamagedArchiveKeepsTheOldTree() {
        install("v1")
        val bad = tree("biber v2").copyOf(40)
        try {
            install("v2", bad)
            fail("no exception")
        } catch (_: IOException) {
        }
        assertEquals("v1", Trees.readMarker(home))
        assertFalse(Trees.freshDir(home).exists())
    }

    @Test
    fun anEmptyOrMissingMarkerIsNoVersion() {
        assertNull(Trees.readMarker(home))
        home.mkdirs()
        File(home, Trees.MARKER).writeText("\n")
        assertNull(Trees.readMarker(home))
    }
}
