package io.github.lavishsonbhurra.biberdroid

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test

class BiberInstallationTest {

    private val root: File = Files.createTempDirectory("biber").toFile()
    private val home = File(root, "files/biber")
    private val launcher = File(root, "lib/arm64/libbiber.so")

    /** An installation as Biber.installation describes one before anything is unpacked. */
    private val biber = Trees.describe(home, launcher, "v1")

    @Test
    fun applyToWorksBeforeAnInstall() {
        val env = mutableMapOf("PATH" to "/system/bin", "LD_LIBRARY_PATH" to "/vendor/lib64")
        biber.applyTo(env)
        assertEquals("${home.path}/bin:/system/bin", env["PATH"])
        assertEquals("${launcher.parent}:/vendor/lib64", env["LD_LIBRARY_PATH"])
        assertEquals(home.path, env["BIBER_HOME"])
        assertFalse("BIBER_CACHE" in env)
    }

    @Test
    fun applyToKeepsOneEntryPerFolder() {
        val env = mutableMapOf("PATH" to "/system/bin:${home.path}/bin", "LD_LIBRARY_PATH" to launcher.parent!!)
        biber.applyTo(env, File(root, "cache"))
        biber.applyTo(env, File(root, "cache"))
        assertEquals("${home.path}/bin:/system/bin", env["PATH"])
        assertEquals(launcher.parent, env["LD_LIBRARY_PATH"])
        assertEquals(File(root, "cache").path, env["BIBER_CACHE"])
    }

    @Test
    fun applyToDefaultsPathToSystemBin() {
        val env = mutableMapOf<String, String>()
        biber.applyTo(env)
        assertEquals("${home.path}/bin:/system/bin", env["PATH"])
    }

    @Test
    fun pathsAndCommand() {
        assertEquals(File(home, "bin"), biber.binDir)
        assertEquals(File(home, "bin/tectonic-biber"), biber.tectonicTool)
        assertEquals(listOf(launcher.absolutePath, "--validate-datamodel", "main"), biber.command(listOf("--validate-datamodel", "main")))
        assertTrue(biber.toString().contains("isCurrent=false"))
    }

    @Test
    fun processBuilderHasItsOwnEnvironment() {
        val work = File(root, "doc")
        val tmp = File(root, "cache")
        val pb = Trees.processBuilder(biber, work, listOf("main"), tmp, mapOf("LD_PRELOAD" to "/x/libguard.so", "TMPDIR" to "/t"))
        assertEquals(listOf(launcher.absolutePath, "main"), pb.command())
        assertEquals(work, pb.directory())
        assertTrue(pb.redirectErrorStream())
        assertEquals(File("/dev/null"), pb.redirectInput().file())
        val env = pb.environment()
        assertEquals(
            mapOf(
                "PATH" to "${home.path}/bin:/system/bin",
                "HOME" to work.path,
                "TMPDIR" to "/t",
                "LD_LIBRARY_PATH" to launcher.parent,
                "BIBER_HOME" to home.path,
                "LD_PRELOAD" to "/x/libguard.so",
            ),
            env.toMap(),
        )
    }

    @Test
    fun processBuilderWithoutExtrasUsesTheTmpDir() {
        val pb = Trees.processBuilder(biber, root, listOf("main"), File(root, "cache"), emptyMap())
        assertEquals(File(root, "cache").path, pb.environment()["TMPDIR"])
    }

    @Test
    fun versionsCompareByReleaseAndPayload() {
        val hash = "191d653cadd09d857eee949330fc4d189372caef6d6ec6be0e757f43f69da170"
        val a = BiberVersion("2.21", hash)
        assertEquals(a, BiberVersion("2.21", hash))
        assertEquals(a.hashCode(), BiberVersion("2.21", hash).hashCode())
        assertNotEquals(a, BiberVersion("2.21", "0$hash".take(64)))
        assertEquals("biber 2.21, payload 191d653cadd0", a.toString())
    }

    private fun sh(script: String): Process {
        assumeTrue(File("/bin/sh").canExecute())
        return ProcessBuilder("/bin/sh", "-c", script).redirectErrorStream(true).start()
    }

    @Test
    fun waitForCollectsOutputAndExitCode() {
        val r = Biber.waitFor(sh("echo out; echo err >&2; exit 3"), 10_000)
        assertEquals(3, r.exitCode)
        assertFalse(r.timedOut)
        assertFalse(r.succeeded)
        assertEquals("out\nerr\n", r.output)
    }

    @Test
    fun waitForStopsAProcessPastItsTimeout() {
        val r = Biber.waitFor(sh("echo started; exec sleep 30"), 300)
        assertTrue(r.timedOut)
        assertEquals(-1, r.exitCode)
        assertEquals("started\n", r.output)
    }

    @Test
    fun waitForCutsOutputAtOneMebibyte() {
        val r = Biber.waitFor(sh("head -c 1100000 /dev/zero | tr '\\0' x"), 10_000)
        assertTrue(r.succeeded)
        assertEquals(1 shl 20, r.output.length)
    }
}
