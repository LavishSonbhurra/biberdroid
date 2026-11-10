package io.github.lavishsonbhurra.biber

import java.io.File

/**
 * An installed biber, as [Biber.install] returns it.
 *
 * @property home the unpacked Perl tree (`bin/biber`, `lib/`), and `bin/tectonic-biber`.
 * @property launcher `libbiber.so` in the app's `nativeLibraryDir`: the program to start.
 * @property nativeLibraryDir where the launcher, `libperl.so` and the XS modules are.
 * @property version the installed tree's version (the SHA-256 of the archive it came from).
 */
public class BiberInstallation internal constructor(
    public val home: File,
    public val launcher: File,
    public val nativeLibraryDir: File,
    public val version: String,
) {
    /** Holds the `tectonic-biber` link; put it on `PATH` for Tectonic. */
    public val binDir: File get() = File(home, "bin")

    /** A symlink to [launcher]. Started under this name, biber works as Tectonic's external tool. */
    public val tectonicTool: File get() = File(binDir, Biber.TECTONIC_TOOL)

    /** The command line that runs biber with [args]. */
    public fun command(args: List<String>): List<String> = listOf(launcher.absolutePath) + args

    /**
     * Sets up [env] (a `ProcessBuilder`'s environment, say) so that biber runs and a TeX
     * engine finds it: [binDir] first on `PATH`, [nativeLibraryDir] on `LD_LIBRARY_PATH`,
     * and `BIBER_HOME`. With [cacheDir], `BIBER_CACHE` too: started as `tectonic-biber`,
     * the launcher then reuses an earlier result for identical input without starting
     * Perl. The folder is made when needed and holds at most 8 results.
     */
    @JvmOverloads
    public fun applyTo(env: MutableMap<String, String>, cacheDir: File? = null) {
        env["PATH"] = prepend(binDir.absolutePath, env["PATH"] ?: "/system/bin")
        env["LD_LIBRARY_PATH"] = prepend(nativeLibraryDir.absolutePath, env["LD_LIBRARY_PATH"])
        env["BIBER_HOME"] = home.absolutePath
        if (cacheDir != null) env["BIBER_CACHE"] = cacheDir.absolutePath
    }

    private fun prepend(dir: String, list: String?): String =
        if (list.isNullOrEmpty()) dir else dir + ":" + list.split(':').filter { it != dir }.joinToString(":")
}

/**
 * What a run of biber gave.
 *
 * @property exitCode biber's exit status, or -1 when it was stopped after the timeout.
 * @property output its standard output and error, interleaved (cut at 1 MiB).
 * @property timedOut whether it was stopped because it ran past the timeout.
 */
public data class BiberResult(
    public val exitCode: Int,
    public val output: String,
    public val timedOut: Boolean,
) {
    /** biber finished in time with exit status 0. */
    public val succeeded: Boolean get() = exitCode == 0 && !timedOut
}
