package io.github.lavishsonbhurra.biberdroid

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.tukaani.xz.XZInputStream

class TarTest {

    /** A ustar header as Python's tarfile writes one, with a correct checksum. */
    private fun header(name: String, type: Char, size: Long, prefix: String = ""): ByteArray {
        val h = ByteArray(512)
        fun put(off: Int, s: String) = s.toByteArray().copyInto(h, off)
        put(0, name)
        put(100, "0000644\u0000")
        put(108, "0000000\u0000")
        put(116, "0000000\u0000")
        put(124, "%011o\u0000".format(size))
        put(136, "00000000000\u0000")
        h[156] = type.code.toByte()
        put(257, "ustar\u0000")
        put(263, "00")
        put(345, prefix)
        for (i in 148 until 156) h[i] = ' '.code.toByte()
        val sum = h.sumOf { it.toInt() and 0xff }
        put(148, "%06o\u0000 ".format(sum))
        return h
    }

    private fun archive(vararg entries: Triple<String, Char, ByteArray>): ByteArray {
        val out = ByteArrayOutputStream()
        for ((name, type, body) in entries) {
            out.write(header(name, type, body.size.toLong()))
            out.write(body)
            out.write(ByteArray(((512 - body.size % 512) % 512)))
        }
        out.write(ByteArray(1024))
        return out.toByteArray()
    }

    private fun read(bytes: ByteArray): Map<String, String?> {
        val seen = LinkedHashMap<String, String?>()
        Tar.read(ByteArrayInputStream(bytes)) { e, body ->
            seen[e.name] = if (e.isDirectory) null else body.readBytes().decodeToString()
        }
        return seen
    }

    @Test
    fun readsFoldersAndFiles() {
        val tar = archive(
            Triple("lib/", '5', ByteArray(0)),
            Triple("lib/Biber.pm", '0', "package Biber;\n".toByteArray()),
            Triple("bin/biber", '0', ByteArray(1000) { 'x'.code.toByte() }),
        )
        val seen = read(tar)
        assertEquals(listOf("lib", "lib/Biber.pm", "bin/biber"), seen.keys.toList())
        assertEquals(null, seen["lib"])
        assertEquals("package Biber;\n", seen["lib/Biber.pm"])
        assertEquals(1000, seen["bin/biber"]!!.length)
    }

    @Test
    fun joinsTheUstarPrefix() {
        val out = ByteArrayOutputStream()
        out.write(header("Collate.pm", '0', 0, prefix = "lib/Unicode"))
        out.write(ByteArray(1024))
        assertEquals(listOf("lib/Unicode/Collate.pm"), read(out.toByteArray()).keys.toList())
    }

    @Test
    fun refusesNamesThatLeaveTheFolder() {
        for (name in listOf("../evil", "lib/../../evil", "/etc/passwd", "./x", "a//b")) {
            try {
                read(archive(Triple(name, '0', ByteArray(1))))
                fail("accepted $name")
            } catch (e: IOException) {
                assertTrue(e.message!!.startsWith("unsafe"))
            }
        }
    }

    @Test
    fun refusesLinksAndOtherTypes() {
        for (type in listOf('1', '2', '3', '6')) {
            try {
                read(archive(Triple("lib/x", type, ByteArray(0))))
                fail("accepted type $type")
            } catch (e: IOException) {
                assertTrue(e.message!!.startsWith("unsupported"))
            }
        }
    }

    @Test
    fun refusesDamagedOrCutArchives() {
        val good = archive(Triple("lib/a.pm", '0', "1;\n".toByteArray()))
        val damaged = good.copyOf().also { it[0] = 'X'.code.toByte() }
        for (bad in listOf(damaged, good.copyOf(700), good.copyOf(512 + 512))) {
            try {
                read(bad)
                fail("accepted a bad archive of ${bad.size} bytes")
            } catch (_: IOException) {
            }
        }
    }

    @Test
    fun safeNames() {
        assertTrue(Tar.isSafe("lib/Biber/Config.pm"))
        assertTrue(Tar.isSafe(".version"))
        assertFalse(Tar.isSafe(""))
        assertFalse(Tar.isSafe("lib\\x"))
        assertFalse(Tar.isSafe("lib/./x"))
    }

    /** The payload's own tree, when the build has one (the Gradle build passes its path). */
    @Test
    fun readsThePayloadTree() {
        val path = System.getProperty("biber.tree")
        assumeTrue(path != null && java.io.File(path).isFile)
        var files = 0
        var biber = false
        XZInputStream(java.io.File(path!!).inputStream().buffered()).use { xz ->
            Tar.read(xz) { e, body ->
                if (!e.isDirectory) files++
                if (e.name == "bin/biber") biber = body.readBytes().decodeToString().contains("Biber")
            }
        }
        assertTrue("only $files files", files > 500)
        assertTrue("bin/biber missing or not biber's script", biber)
    }
}
