package io.github.lavishsonbhurra.biberdroid

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** With a shell script in the launcher's place: what the library starts, and how it ends it. */
class BiberWarmTest {

    private val root: File = Files.createTempDirectory("warm").toFile()
    private val seen = File(root, "seen.txt")

    private fun launcher(body: String): File = File(root, "libbiber.so").apply {
        writeText("#!/bin/sh\n$body\n")
        setExecutable(true)
    }

    /**
     * Says it is ready as the launcher does, then lives until its standard input ends or
     * SIGTERM comes, and like the launcher ends by itself then.
     */
    private val wellBehaved = """
        trap : TERM
        /usr/bin/env > '${seen.path}'; echo "args: $*" >> '${seen.path}'
        echo "biber: warm process ready at ${'$'}BIBER_WARM, compiled in 1 ms"
        while read -r line; do :; done
        echo ended >> '${seen.path}'
    """.trimIndent()

    /** Waits for the script to have written its last line, which it does when it was told to end. */
    private fun awaitEnd(): Boolean {
        repeat(200) {
            if (seen.isFile && seen.readLines().lastOrNull() == "ended") return true
            Thread.sleep(25)
        }
        return false
    }

    @Test
    fun startsTheLauncherWithExactlyTheEnvironmentGiven() {
        val env = mapOf("PATH" to "/usr/bin:/bin", "BIBER_HOME" to "/somewhere", "BIBER_WARM_IDLE" to "9")
        BiberWarm.start(launcher(wellBehaved), env, 120).use { warm ->
            assertTrue(warm.awaitReady(5_000))
            assertTrue(warm.isAlive)
            val lines = seen.readLines()
            assertTrue("args: --biberdroid-warm" in lines)
            assertTrue("BIBER_WARM=${warm.address}" in lines)
            assertTrue("BIBER_WARM_IDLE=120" in lines)
            assertTrue("BIBER_HOME=/somewhere" in lines)
            // Nothing of the test's own environment.
            assertFalse(lines.any { it.startsWith("HOME=") || it.startsWith("USER=") })
            assertTrue(warm.output.contains("ready at ${warm.address}"))
        }
    }

    @Test
    fun noIdleLimitMeansNoVariable() {
        BiberWarm.start(launcher(wellBehaved), mapOf("PATH" to "/usr/bin:/bin", "BIBER_WARM_IDLE" to "9"), 0).use { warm ->
            assertTrue(warm.awaitReady(5_000))
            assertFalse(seen.readLines().any { it.startsWith("BIBER_WARM_IDLE=") })
        }
    }

    @Test
    fun closeEndsTheProcessByItsStandardInput() {
        // Deaf to SIGTERM: the end of its input is all that ends it.
        val warm = BiberWarm.start(launcher(wellBehaved.replace("trap : TERM", "trap '' TERM")), mapOf("PATH" to "/usr/bin:/bin"), 0)
        assertTrue(warm.awaitReady(5_000))
        assertFalse(warm.awaitEnd(100))
        warm.close()
        warm.close()
        assertTrue(warm.awaitEnd(5_000))
        assertTrue(awaitEnd())
        // It ended by itself, having seen the end of its input: it was not killed.
        assertEquals("ended", seen.readLines().last())
        assertFalse(warm.awaitReady(100))
    }

    @Test
    fun closeEndsAProcessWhoseStandardInputNeverEnds() {
        // As when another process holds the pipe: this one does not look at its input at all.
        val deaf = """
            trap 'echo ended >> "${seen.path}"; exit 0' TERM
            echo started > '${seen.path}'
            echo "biber: warm process ready at ${'$'}BIBER_WARM, compiled in 1 ms"
            while :; do sleep 0.05; done
        """.trimIndent()
        val warm = BiberWarm.start(launcher(deaf), mapOf("PATH" to "/usr/bin:/bin"), 0)
        assertTrue(warm.awaitReady(5_000))
        assertFalse(warm.awaitEnd(200))
        warm.close()
        assertFalse(warm.isAlive)
        assertTrue(warm.awaitEnd(5_000))
        // By SIGTERM, which it handled: it was not killed outright.
        assertEquals("ended", seen.readLines().last())
    }

    @Test
    fun ensureRunningStartsItAgainAtTheSameAddress() {
        val warm = BiberWarm.start(launcher(wellBehaved), mapOf("PATH" to "/usr/bin:/bin", "KEPT" to "yes"), 0)
        assertTrue(warm.awaitReady(5_000))
        val address = warm.address
        warm.ensureRunning()
        // Running: nothing was started, so the script was not run a second time.
        assertEquals(1, seen.readLines().count { it.startsWith("args:") })
        warm.close()
        assertTrue(awaitEnd())
        seen.delete()
        warm.ensureRunning()
        assertTrue(warm.awaitReady(5_000))
        assertTrue(warm.isAlive)
        assertEquals(address, warm.address)
        val lines = seen.readLines()
        assertTrue("BIBER_WARM=$address" in lines)
        assertTrue("KEPT=yes" in lines)
        warm.close()
        assertTrue(awaitEnd())
    }

    @Test
    fun ensureRunningRightAfterCloseStartsANewProcess() {
        // Slow to end, as a process that finishes a run: the new one does not wait for that.
        val slow = wellBehaved + "\nsleep 5"
        val warm = BiberWarm.start(launcher(slow), mapOf("PATH" to "/usr/bin:/bin"), 0)
        assertTrue(warm.awaitReady(5_000))
        warm.close()
        assertFalse(warm.isAlive)
        seen.delete()
        val start = System.nanoTime()
        warm.ensureRunning()
        assertTrue(warm.awaitReady(5_000))
        assertTrue((System.nanoTime() - start) / 1_000_000 < 3_500)
        assertTrue(warm.isAlive)
        assertTrue("BIBER_WARM=${warm.address}" in seen.readLines())
        warm.close()
    }

    /** A process that counts its starts in [seen], and when told to end lives on for [lingerSeconds]. */
    private fun lingering(lingerSeconds: Int) = """
        trap : TERM
        echo start >> '${seen.path}'
        echo "biber: warm process ready at ${'$'}BIBER_WARM, compiled in 1 ms"
        while read -r line; do :; done
        sleep $lingerSeconds
    """.trimIndent()

    /** Runs ensureRunning on a thread of its own and returns once it waits for the process before. */
    private fun ensureRunningElsewhere(warm: BiberWarm): Thread {
        val thread = Thread { warm.ensureRunning() }
        thread.start()
        repeat(200) {
            if (thread.state == Thread.State.TIMED_WAITING) return thread
            Thread.sleep(5)
        }
        return thread
    }

    @Test
    fun othersAreNotKeptWaitingWhileEnsureRunningWaits() {
        val warm = BiberWarm.start(launcher(lingering(3)), mapOf("PATH" to "/usr/bin:/bin"), 0)
        assertTrue(warm.awaitReady(5_000))
        warm.close()
        val thread = ensureRunningElsewhere(warm)
        assertEquals(Thread.State.TIMED_WAITING, thread.state)
        val start = System.nanoTime()
        assertFalse(warm.isAlive)
        assertFalse(warm.awaitReady(0))
        assertTrue(warm.output.contains("ready at"))
        warm.toString()
        assertTrue((System.nanoTime() - start) / 1_000_000 < 400)
        thread.join(10_000)
        assertTrue(warm.isAlive)
        warm.close()
    }

    @Test
    fun closeWhileEnsureRunningWaitsLeavesNoProcessRunning() {
        val warm = BiberWarm.start(launcher(lingering(2)), mapOf("PATH" to "/usr/bin:/bin"), 0)
        assertTrue(warm.awaitReady(5_000))
        warm.close()
        val thread = ensureRunningElsewhere(warm)
        assertEquals(Thread.State.TIMED_WAITING, thread.state)
        warm.close()
        thread.join(10_000)
        assertFalse(thread.isAlive)
        // The process it started was told to end as well.
        assertFalse(warm.isAlive)
        assertTrue(warm.awaitEnd(10_000))
    }

    @Test
    fun ensureRunningFromSeveralThreadsStartsOneProcess() {
        val warm = BiberWarm.start(launcher(lingering(0)), mapOf("PATH" to "/usr/bin:/bin"), 0)
        assertTrue(warm.awaitReady(5_000))
        warm.close()
        assertTrue(warm.awaitEnd(5_000))
        val threads = List(8) { Thread { warm.ensureRunning() } }
        threads.forEach { it.start() }
        threads.forEach { it.join(10_000) }
        assertTrue(warm.awaitReady(5_000))
        assertTrue(warm.isAlive)
        assertEquals(2, seen.readLines().count { it == "start" })
        warm.close()
    }

    @Test
    fun aLauncherThatCannotStartIsNotWaitedFor() {
        val warm = BiberWarm.start(launcher("echo 'biber: no tree' >&2; exit 2"), mapOf("PATH" to "/usr/bin:/bin"), 0)
        val start = System.nanoTime()
        assertFalse(warm.awaitReady(10_000))
        assertTrue((System.nanoTime() - start) / 1_000_000 < 5_000)
        var waited = 0
        while (warm.isAlive && waited++ < 200) Thread.sleep(25)
        assertFalse(warm.isAlive)
        assertEquals("biber: no tree", warm.output)
        warm.close()
    }

    @Test
    fun awaitReadyGivesUpOnASilentProcess() {
        BiberWarm.start(launcher("while read -r line; do :; done"), mapOf("PATH" to "/usr/bin:/bin"), 0).use { warm ->
            assertFalse(warm.awaitReady(200))
            assertTrue(warm.isAlive)
        }
    }

    @Test
    fun applyToNamesTheProcess() {
        BiberWarm.start(launcher(wellBehaved), mapOf("PATH" to "/usr/bin:/bin"), 0).use { warm ->
            val env = mutableMapOf("PATH" to "/usr/bin:/bin", "BIBER_WARM" to "@another")
            warm.applyTo(env)
            assertEquals(warm.address, env["BIBER_WARM"])
        }
    }

    @Test
    fun servesTheEnvironmentItWasStartedWith() {
        val env = mapOf("PATH" to "/usr/bin:/bin", "BIBER_HOME" to "/somewhere", "BIBER_CACHE" to "/cache/one")
        BiberWarm.start(launcher(wellBehaved), env, 0).use { warm ->
            assertTrue(warm.serves(env))
            // What only a run's own process reads does not count.
            assertTrue(warm.serves(env + mapOf("BIBER_WARM" to warm.address, "BIBER_CACHE" to "/cache/two",
                "BIBER_TIMING" to "1", "BIBER_WARM_WAIT" to "500", "PWD" to "/x", "OLDPWD" to "/y", "SHLVL" to "2", "_" to "z")))
            assertTrue(warm.serves(env - "BIBER_CACHE"))
            // Anything else does.
            assertFalse(warm.serves(env + ("BIBER_HOME" to "/elsewhere")))
            assertFalse(warm.serves(env + ("EXTRA" to "1")))
            assertFalse(warm.serves(env - "PATH"))
        }
    }

    @Test
    fun everyProcessHasItsOwnAddress() {
        val a = BiberWarm.newAddress()
        val b = BiberWarm.newAddress()
        assertNotEquals(a, b)
        assertTrue(Regex("@biberdroid-[0-9a-f]{32}").matches(a))
        // Fits a socket address (108 bytes) with room to spare.
        assertTrue(a.length < 64)
    }
}
