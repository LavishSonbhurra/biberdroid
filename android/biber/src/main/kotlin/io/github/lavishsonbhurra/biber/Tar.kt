package io.github.lavishsonbhurra.biber

import java.io.EOFException
import java.io.IOException
import java.io.InputStream

/**
 * Reads a ustar archive as scripts/archive.py writes one. Only folders and
 * regular files are accepted; anything else, a name that could leave the
 * target folder, or a damaged header fails with an IOException, so a bad
 * archive never half-works.
 */
internal object Tar {

    class Entry(val name: String, val isDirectory: Boolean, val size: Long)

    private const val BLOCK = 512

    /** Calls [onEntry] for each entry; for a file, the [InputStream] gives exactly its bytes. */
    fun read(input: InputStream, onEntry: (Entry, InputStream) -> Unit) {
        val header = ByteArray(BLOCK)
        while (true) {
            if (!fill(input, header)) throw EOFException("tar ended without its end marker")
            if (header.all { it == 0.toByte() }) return
            if (!checksumOk(header)) throw IOException("damaged tar header")
            val name = name(header)
            val type = header[156].toInt().toChar()
            val size = octal(header, 124, 12)
            val dir = type == '5'
            if (!dir && type != '0' && type != '\u0000') throw IOException("unsupported tar entry $name ($type)")
            val clean = name.trimEnd('/')
            if (!isSafe(clean)) throw IOException("unsafe tar entry $name")
            val body = Bounded(input, if (dir) 0 else size)
            onEntry(Entry(clean, dir, if (dir) 0 else size), body)
            body.skipRest()
            if (!dir) skipFully(input, (BLOCK - size % BLOCK) % BLOCK)
        }
    }

    /** A relative path that stays inside the folder it is unpacked into. */
    fun isSafe(name: String): Boolean =
        name.isNotEmpty() && !name.startsWith("/") && '\\' in name == false && '\u0000' in name == false &&
            name.split('/').all { it.isNotEmpty() && it != "." && it != ".." }

    private fun name(h: ByteArray): String {
        val base = text(h, 0, 100)
        val ustar = text(h, 257, 6) == "ustar"
        val prefix = if (ustar) text(h, 345, 155) else ""
        return if (prefix.isEmpty()) base else "$prefix/$base"
    }

    private fun text(h: ByteArray, off: Int, len: Int): String {
        var end = off
        while (end < off + len && h[end] != 0.toByte()) end++
        return String(h, off, end - off, Charsets.UTF_8)
    }

    private fun octal(h: ByteArray, off: Int, len: Int): Long {
        val t = text(h, off, len).trim()
        if (t.isEmpty()) return 0
        if (t.any { it !in '0'..'7' }) throw IOException("damaged tar header")
        return t.toLong(8)
    }

    private fun checksumOk(h: ByteArray): Boolean {
        var sum = 0L
        for (i in 0 until BLOCK) sum += if (i in 148 until 156) ' '.code else (h[i].toInt() and 0xff)
        return try {
            octal(h, 148, 8) == sum
        } catch (_: IOException) {
            false
        }
    }

    private fun fill(input: InputStream, buf: ByteArray): Boolean {
        var off = 0
        while (off < buf.size) {
            val n = input.read(buf, off, buf.size - off)
            if (n < 0) {
                if (off == 0) return false
                throw EOFException("tar cut short")
            }
            off += n
        }
        return true
    }

    private fun skipFully(input: InputStream, count: Long) {
        var left = count
        val buf = ByteArray(BLOCK)
        while (left > 0) {
            val n = input.read(buf, 0, minOf(left, BLOCK.toLong()).toInt())
            if (n < 0) throw EOFException("tar cut short")
            left -= n
        }
    }

    /** One entry's bytes; closing it leaves the archive open. */
    private class Bounded(private val input: InputStream, private var left: Long) : InputStream() {
        override fun read(): Int {
            if (left <= 0) return -1
            val b = input.read()
            if (b < 0) throw EOFException("tar cut short")
            left--
            return b
        }

        override fun read(b: ByteArray, off: Int, len: Int): Int {
            if (left <= 0) return -1
            val n = input.read(b, off, minOf(len.toLong(), left).toInt())
            if (n < 0) throw EOFException("tar cut short")
            left -= n
            return n
        }

        override fun close() {}

        fun skipRest() = skipFully(input, left).also { left = 0 }
    }
}
