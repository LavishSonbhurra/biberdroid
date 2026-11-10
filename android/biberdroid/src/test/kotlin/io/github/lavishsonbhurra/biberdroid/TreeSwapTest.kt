package io.github.lavishsonbhurra.biberdroid

import java.io.File
import java.io.IOException
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.fail
import org.junit.Test

class TreeSwapTest {

    private fun tree(dir: File, marker: String): File {
        File(dir, "lib").mkdirs()
        File(dir, ".version").writeText(marker)
        return dir
    }

    private fun folders(): Triple<File, File, File> {
        val root = Files.createTempDirectory("swap").toFile()
        return Triple(File(root, "biber.unpacking"), File(root, "biber"), File(root, "biber.old"))
    }

    @Test
    fun replacesTheInstalledTree() {
        val (fresh, home, old) = folders()
        tree(home, "v1")
        tree(fresh, "v2")
        TreeSwap.swap(fresh, home, old)
        assertEquals("v2", File(home, ".version").readText())
        assertFalse(fresh.exists())
        assertFalse(old.exists())
    }

    @Test
    fun installsWhenThereIsNoTree() {
        val (fresh, home, old) = folders()
        tree(fresh, "v1")
        TreeSwap.swap(fresh, home, old)
        assertEquals("v1", File(home, ".version").readText())
    }

    @Test
    fun putsTheOldTreeBackWhenTheNewOneCannotMove() {
        val (fresh, home, old) = folders()
        tree(home, "v1")
        tree(fresh, "v2")
        val failFresh = { from: File, to: File -> if (from == fresh) false else from.renameTo(to) }
        try {
            TreeSwap.swap(fresh, home, old, failFresh)
            fail("no exception")
        } catch (e: IOException) {
            assertEquals("Could not move the tree into place", e.message)
        }
        assertEquals("v1", File(home, ".version").readText())
        assertFalse(fresh.exists())
        assertFalse(old.exists())
    }

    @Test
    fun keepsTheTreeWhenItCannotBeMovedAside() {
        val (fresh, home, old) = folders()
        tree(home, "v1")
        tree(fresh, "v2")
        try {
            TreeSwap.swap(fresh, home, old) { _, _ -> false }
            fail("no exception")
        } catch (_: IOException) {
        }
        assertEquals("v1", File(home, ".version").readText())
        assertFalse(fresh.exists())
    }
}
