package io.github.lavishsonbhurra.biberdroid

import java.io.Closeable
import java.io.File
import java.io.IOException
import java.security.SecureRandom
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

private const val ENV_ADDRESS = "BIBER_WARM"
private const val ENV_IDLE = "BIBER_WARM_IDLE"

/**
 * A warm biber process: the launcher with biber's Perl modules already compiled, which
 * does the runs that are handed to it. A run then takes about half as long, because over
 * half of a run on its own is Perl compiling those modules. [Biber.warm] starts one.
 *
 * A run is handed over when its environment has `BIBER_WARM` set to this process's
 * [address] ([applyTo] sets it) and is otherwise the environment this process was started
 * with ([serves]). Such a run behaves as it would on its own: same output, exit status,
 * working folder and files. Any other run, and any run that finds this process gone,
 * busy or not answering, is done on its own, as if `BIBER_WARM` were not set: nothing
 * needs to be checked before a compile.
 *
 * The process holds memory while it waits (about 80 MB resident on a 64-bit device,
 * measured off-device), so keep one only while runs are likely: start it when a compile
 * that needs biber begins, [close] it when the document is closed or the app leaves the
 * foreground. It also ends on its own after the idle time given to [Biber.warm], and
 * when the app's process dies. Starting it costs about as much processor time as one
 * biber run's compile phase, on another core than the TeX engine's.
 *
 * An object keeps its [address] for life, and [ensureRunning] starts the process again
 * after it has ended. So an environment set up once (a long-lived engine process, say)
 * stays valid: call [ensureRunning] before each compile and [close] when compiling stops.
 *
 * Objects of this class are safe to use from several threads.
 *
 * @property address the value of `BIBER_WARM` that leads a run to this process: a name in
 *   Linux's abstract socket namespace, made up for this object from 128 random bits.
 *   Nothing is left in the file system. The namespace itself keeps nobody out; the process
 *   and each run check that the other side runs as the same user, and use nothing else.
 */
public class BiberWarm internal constructor(
    private val launcher: File,
    environment: Map<String, String>,
    private val idleSeconds: Int,
    public val address: String,
) : Closeable {

    private val environment: Map<String, String> = HashMap(environment)
    private val counted: Map<String, String> = counted(environment)

    /** One start of the process: it, and what its output said. */
    private class Run(val process: Process) {
        val ready = CountDownLatch(1)
        val ended = CountDownLatch(1)
        val lines = ArrayDeque<String>()

        /** Told to end by [close]: it may still be finishing runs, but takes no new ones. */
        @Volatile
        var closed = false
    }

    private var run: Run? = null

    /**
     * Whether the process is running and takes runs. False after [close] (even while it
     * finishes runs it had), once it has ended after its idle time, and when it could not
     * start (biber not installed, say). Runs do not need it to be true; [ensureRunning]
     * starts the process again.
     */
    public val isAlive: Boolean get() = synchronized(this) { run }?.let { !it.closed && it.process.isAlive } == true

    /**
     * Starts the process unless it is running, at the same [address] and with the same
     * environment as before, and returns at once: the process compiles in the background.
     * Call it before a compile that may need biber; when the process is running this costs
     * nothing.
     *
     * @throws IOException when the process cannot be started.
     */
    @Throws(IOException::class)
    public fun ensureRunning() {
        synchronized(this) {
            val current = run
            if (current != null && current.process.isAlive) {
                if (!current.closed) return
                // Told to end: it gives up the address as soon as it sees its input end, and
                // ends when its runs have. Mostly it is gone at once; a moment for it, so that
                // the new process finds the address free. Should it not (then the new one
                // ends at once, and isAlive says so), the next call starts another.
                try {
                    current.process.waitFor(GIVE_UP_MILLIS, TimeUnit.MILLISECONDS)
                } catch (_: InterruptedException) {
                    Thread.currentThread().interrupt()
                }
            }
            val pb = ProcessBuilder(launcher.absolutePath, "--biberdroid-warm").redirectErrorStream(true)
            val env = pb.environment()
            env.clear()
            env.putAll(environment)
            env[ENV_ADDRESS] = address
            if (idleSeconds > 0) env[ENV_IDLE] = idleSeconds.toString() else env.remove(ENV_IDLE)
            val started = Run(pb.start())
            run = started
            val reader = Thread({ read(started) }, "biber-warm")
            reader.isDaemon = true
            reader.start()
        }
    }

    private fun read(run: Run) {
        try {
            run.process.inputStream.bufferedReader().useLines { all ->
                for (line in all) {
                    synchronized(run.lines) {
                        if (run.lines.size == KEPT_LINES) run.lines.removeFirst()
                        run.lines.addLast(line)
                    }
                    if (READY in line) run.ready.countDown()
                }
            }
        } catch (_: IOException) {
            // The process is gone.
        } finally {
            run.ended.countDown()
        }
    }

    /**
     * Sets `BIBER_WARM` in [env] (a `ProcessBuilder`'s environment, say), so that biber
     * runs started with it, directly or by a TeX engine, are handed to this process.
     */
    public fun applyTo(env: MutableMap<String, String>) {
        env[ENV_ADDRESS] = address
    }

    /**
     * Whether a run with [environment] would be done by this process: true when it is the
     * environment this process was started with, not counting the variables only a run's
     * own process reads (`BIBER_WARM`, `BIBER_WARM_WAIT`, `BIBER_WARM_IDLE`, `BIBER_CACHE`,
     * `BIBER_TIMING`) and those a shell sets (`PWD`, `OLDPWD`, `SHLVL`, `_`). When this is
     * false for the environment of the next compile, [close] this object and make one
     * with [Biber.warm] for that environment.
     */
    public fun serves(environment: Map<String, String>): Boolean = counted(environment) == counted

    /**
     * Waits until the process has compiled biber and takes runs, at most [timeoutMillis].
     * Returns false when it has not by then, or has ended. There is no need to wait before
     * a compile: a run that comes early waits for the process itself, up to three seconds.
     */
    @Throws(InterruptedException::class)
    public fun awaitReady(timeoutMillis: Long): Boolean {
        val run = synchronized(this) { run } ?: return false
        val deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeoutMillis)
        while (run.ready.count != 0L) {
            val left = deadline - System.nanoTime()
            if (left <= 0 || run.ended.count == 0L) return false
            run.ready.await(minOf(left, TimeUnit.MILLISECONDS.toNanos(20)), TimeUnit.NANOSECONDS)
        }
        return !run.closed && run.process.isAlive
    }

    /**
     * Waits until the process started last has ended, at most [timeoutMillis], and says
     * whether it has: after [close], to see that its memory is free again. A process that
     * still does a run ends when the run does. True when none was ever started.
     */
    @Throws(InterruptedException::class)
    public fun awaitEnd(timeoutMillis: Long): Boolean {
        val run = synchronized(this) { run } ?: return true
        return run.process.waitFor(timeoutMillis, TimeUnit.MILLISECONDS)
    }

    /** What the process printed last (a few lines at most), for logs: why it did not start, say. */
    public val output: String
        get() {
            val run = synchronized(this) { run } ?: return ""
            return synchronized(run.lines) { run.lines.joinToString("\n") }
        }

    /**
     * Tells the process to end, and returns at once. Runs it is doing finish first; runs
     * that come later are done on their own, until [ensureRunning] starts a process
     * again. Calling it again does nothing.
     */
    override fun close() {
        val run = synchronized(this) { run } ?: return
        run.closed = true
        try {
            // The end of its standard input is what the process waits for.
            run.process.outputStream.close()
        } catch (_: IOException) {
            // Already ended.
        }
    }

    /** For logs: the address and whether the process is running. */
    override fun toString(): String = "BiberWarm(address=$address, isAlive=$isAlive)"

    internal companion object {
        private const val READY = "warm process ready"
        private const val KEPT_LINES = 8
        private const val GIVE_UP_MILLIS = 1000L
        private val CLIENT_ONLY =
            setOf(ENV_ADDRESS, "BIBER_WARM_WAIT", ENV_IDLE, "BIBER_CACHE", "BIBER_TIMING", "PWD", "OLDPWD", "SHLVL", "_")
        private val random = SecureRandom()

        private fun counted(environment: Map<String, String>): Map<String, String> =
            environment.filterKeys { it !in CLIENT_ONLY }

        /** A name in the abstract socket namespace that no other process can guess. */
        fun newAddress(): String {
            val bytes = ByteArray(16).also { random.nextBytes(it) }
            return "@biberdroid-" + bytes.joinToString("") { "%02x".format(it) }
        }

        /**
         * Starts [launcher] as a warm process with exactly [environment] (nothing is
         * inherited from this process), plus its address and idle time.
         */
        fun start(launcher: File, environment: Map<String, String>, idleSeconds: Int): BiberWarm =
            BiberWarm(launcher, environment, idleSeconds, newAddress()).also { it.ensureRunning() }
    }
}
