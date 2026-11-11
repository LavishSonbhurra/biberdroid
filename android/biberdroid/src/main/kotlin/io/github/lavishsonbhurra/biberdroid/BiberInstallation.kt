package io.github.lavishsonbhurra.biberdroid

import java.io.File

/**
 * Where biber lives in an app, and the state of its Perl tree when this object was made.
 * [Biber.install] returns one after installing; [Biber.installation] describes the
 * current state without unpacking anything.
 *
 * The paths never change for a given install of the app. The state ([installedVersion],
 * [isInstalled], [isCurrent]) is a snapshot: ask [Biber.installation] again for a fresh
 * one. Objects of this class are immutable and safe to share between threads.
 *
 * @property home the Perl tree (`bin/biber`, `lib/`), and `bin/tectonic-biber`. It does
 *   not exist until the first install.
 * @property launcher `libbiber.so` in the app's `nativeLibraryDir`: the program to start.
 * @property nativeLibraryDir where the launcher, `libperl.so` and the XS modules are.
 */
public class BiberInstallation internal constructor(
    public val home: File,
    public val launcher: File,
    public val nativeLibraryDir: File,
    installedVersion: String?,
    private val shippedPayload: String,
    current: Boolean,
) {
    /**
     * The version of the tree in [home] (the SHA-256 of the archive it was unpacked from,
     * as in [BiberVersion.payload]), or null when no tree is installed.
     */
    public val installedVersion: String? = installedVersion

    /** Whether a tree, of any version, is installed in [home]. */
    public val isInstalled: Boolean get() = installedVersion != null

    /**
     * Whether [Biber.install] would have nothing to do: the tree in [home] is the version
     * this library ships, and `bin/tectonic-biber` points at this install's [launcher].
     */
    public val isCurrent: Boolean = current

    /**
     * The tree's version.
     *
     * For an object from [Biber.install] this is the installed version. Otherwise it is
     * [installedVersion] when a tree is installed, and the version an install would write
     * when none is.
     */
    @Deprecated(
        "Ambiguous before an install. Use installedVersion, or Biber.shippedVersion(context).payload.",
        ReplaceWith("installedVersion"),
    )
    public val version: String get() = installedVersion ?: shippedPayload

    /** Holds the `tectonic-biber` link; put it on `PATH` for Tectonic. */
    public val binDir: File get() = File(home, "bin")

    /** A symlink to [launcher]. Started under this name, biber works as Tectonic's external tool. */
    public val tectonicTool: File get() = File(binDir, Biber.TECTONIC_TOOL)

    /** The command line that runs biber with [args]: [launcher], then [args]. */
    public fun command(args: List<String>): List<String> = listOf(launcher.absolutePath) + args

    /**
     * Sets up [env] (a `ProcessBuilder`'s environment, say) so that biber runs and a TeX
     * engine finds it: [binDir] first on `PATH`, [nativeLibraryDir] first on
     * `LD_LIBRARY_PATH`, and `BIBER_HOME`. With [cacheDir], `BIBER_CACHE` too: started as
     * `tectonic-biber`, the launcher then reuses an earlier result for identical input
     * without starting Perl. The cache folder is made when needed and holds at most 8
     * results. Entries already on `PATH` or `LD_LIBRARY_PATH` are kept, after the new one.
     *
     * It only sets variables, so it works on an object from [Biber.installation] as well:
     * when no tree is installed yet, Tectonic simply finds no `tectonic-biber`.
     */
    @JvmOverloads
    public fun applyTo(env: MutableMap<String, String>, cacheDir: File? = null) {
        env["PATH"] = prepend(binDir.absolutePath, env["PATH"] ?: "/system/bin")
        env["LD_LIBRARY_PATH"] = prepend(nativeLibraryDir.absolutePath, env["LD_LIBRARY_PATH"])
        env["BIBER_HOME"] = home.absolutePath
        if (cacheDir != null) env["BIBER_CACHE"] = cacheDir.absolutePath
    }

    /** For logs: the paths and the state. */
    override fun toString(): String =
        "BiberInstallation(home=$home, launcher=$launcher, installedVersion=$installedVersion, isCurrent=$isCurrent)"

    /** [dir], then the entries of [list] other than [dir]. An empty entry would mean the current folder. */
    private fun prepend(dir: String, list: String?): String {
        if (list.isNullOrEmpty()) return dir
        val rest = list.split(':').filter { it != dir }
        return if (rest.isEmpty()) dir else dir + ":" + rest.joinToString(":")
    }
}

/**
 * A version of the biber payload: the biber release and the exact Perl tree, as
 * [Biber.shippedVersion] reads it from the library's assets.
 *
 * @property biber the biber release, such as `2.21` ([Biber.VERSION]).
 * @property payload the SHA-256 of the Perl tree archive, 64 hex digits. This is what an
 *   installed tree records ([BiberInstallation.installedVersion]); two builds of the
 *   library with the same payload have the same value.
 */
public class BiberVersion internal constructor(
    public val biber: String,
    public val payload: String,
) {
    /** Equal when both the release and the payload are. */
    override fun equals(other: Any?): Boolean =
        other is BiberVersion && other.biber == biber && other.payload == payload

    /** Consistent with [equals]. */
    override fun hashCode(): Int = biber.hashCode() * 31 + payload.hashCode()

    /** `biber 2.21, payload 191d653cadd0`: the release and the first 12 digits of the hash. */
    override fun toString(): String = "biber $biber, payload ${payload.take(12)}"
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
