package io.github.lavishsonbhurra.biber

import java.io.File
import java.io.IOException

/** Puts a freshly unpacked tree in place of the installed one, by renaming folders. */
internal object TreeSwap {

    /**
     * Moves [home] aside to [old], [fresh] to [home], then deletes [old]. If [fresh]
     * cannot be moved into place, the old tree is moved back, so a failed update leaves
     * the previous install working. [fresh] is deleted whenever it does not end up in place.
     */
    fun swap(fresh: File, home: File, old: File, rename: (File, File) -> Boolean = { from, to -> from.renameTo(to) }) {
        val hadTree = home.exists()
        if (hadTree && !rename(home, old)) {
            fresh.deleteRecursively()
            throw IOException("Could not move the old tree aside")
        }
        if (!rename(fresh, home)) {
            fresh.deleteRecursively()
            if (hadTree && !rename(old, home)) throw IOException("Could not move the tree into place, nor the old one back")
            throw IOException("Could not move the tree into place")
        }
        old.deleteRecursively()
    }
}
