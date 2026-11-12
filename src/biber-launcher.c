/*
 * libbiber.so: biber 2.21 as a single executable for Android.
 *
 * Embeds perl (libperl.so from the same folder) and runs the biber script
 * from the asset tree. Shipped in jniLibs, so it lives in nativeLibraryDir,
 * the only place an app targeting SDK 29+ may execve from. Tectonic finds it
 * through a symlink named tectonic-biber (or biber) on PATH.
 *
 *   BIBER_HOME        asset tree (bin/biber, lib/). Default: the parent of
 *                     the folder holding argv[0], so a symlink at
 *                     $BIBER_HOME/bin/tectonic-biber needs no variable.
 *   BIBER_NATIVE_DIR  where the libpl_*.so XS modules are. Default: the
 *                     folder of /proc/self/exe (nativeLibraryDir).
 *   BIBER_CACHE       optional folder for reusing results, see below.
 *   BIBER_WARM        optional address of a warm process, see below.
 *   BIBER_WARM_WAIT   milliseconds a run waits for the warm process to take
 *                     it before it runs on its own (3000).
 *   BIBER_WARM_IDLE   for the warm process: seconds without a run after
 *                     which it stops (0, the default: never).
 *   BIBER_TIMING      1 prints where the time went on stderr when the run
 *                     ends; an absolute path appends that line to a file.
 *
 * Started as tectonic-biber, it is Tectonic's external tool: Tectonic copies
 * the .bcf and the data files it names into an empty temporary folder, runs
 * the tool there, and takes back every new file it finds. Two things differ
 * from a plain biber run then:
 *
 *  - The .bib copies are removed after the run: the data sources the .bcf
 *    names that were in the folder before biber ran, and only when the folder
 *    is a .tmp* folder directly inside TMPDIR, as Tectonic's are. Tectonic
 *    would otherwise take each .bib back as a new file and write it into the
 *    document's folder, over the user's own file (or, for a .bib from the TeX
 *    bundle, as a new file in the project).
 *  - With BIBER_CACHE, a run whose inputs match an earlier one gives that
 *    run's output without starting perl. The inputs are the arguments and
 *    every file in the folder, which is all biber reads when every data
 *    source the .bcf names is a plain file name present in the folder, plus
 *    the Perl tree's version marker (BIBER_HOME/.version); any other run, or
 *    a tree without a marker, is not cached. Up to CACHE_KEEP results are kept.
 *
 * Over half of a run is perl compiling biber's modules, the same work every
 * time. "libbiber.so --biberdroid-warm" is a warm process: it compiles biber
 * once, listens on the address in BIBER_WARM ("@name", a socket in Linux's
 * abstract namespace, which leaves nothing in the file system, or an absolute
 * path) and forks a worker for each run that asks. A run started with the
 * same BIBER_WARM, under any name, hands itself to that process: its standard
 * descriptors, working folder, arguments, environment, umask, resource
 * limits, priority and signal settings. The worker takes them all on and
 * starts where an ordinary run is after compiling; the run waits, passes on
 * the signals it gets and ends as the worker ended. See "the warm process"
 * below for the rules that keep such a run the same as an ordinary one.
 *
 * The warm process belongs to whoever started it: it stops when its standard
 * input reaches its end (so it never outlives an app that holds the other
 * end of that pipe), on SIGTERM, SIGINT or SIGHUP, or after BIBER_WARM_IDLE
 * seconds without a run. Runs in progress finish first. A run never starts a
 * warm process, and never waits for one that is not there: without one, or
 * with one it may not use, it runs on its own as if BIBER_WARM were not set.
 */
#include <EXTERN.h>
#include <perl.h>
#include <perliol.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Part of every cache key: change it when biber or this launcher changes. */
#define CACHE_VERSION "biber 2.21 launcher 4"
#define CACHE_KEEP 8
#define MAX_FILES 256
#define MAX_INPUT_BYTES (64L << 20)

EXTERN_C void boot_DynaLoader(pTHX_ CV *cv);

static void xs_init(pTHX) {
    newXS("DynaLoader::boot_DynaLoader", boot_DynaLoader, __FILE__);
}

static PerlInterpreter *my_perl;
extern char **environ;

static char *parent_dir(const char *path) {
    char *copy = strdup(path);
    char *slash = strrchr(copy, '/');
    if (!slash) { free(copy); return NULL; }
    if (slash == copy) slash[1] = '\0'; else *slash = '\0';
    return copy;
}

/* argv[0] as a path: a bare name is looked up on PATH, as the shell found it. */
static char *self_path(const char *arg0) {
    if (strchr(arg0, '/')) return strdup(arg0);
    const char *path = getenv("PATH");
    if (!path) return NULL;
    char *copy = strdup(path), *save = NULL, *found = NULL;
    for (char *dir = strtok_r(copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        char try[PATH_MAX];
        snprintf(try, sizeof try, "%s/%s", dir, arg0);
        if (access(try, X_OK) == 0) { found = strdup(try); break; }
    }
    free(copy);
    return found;
}

/* ---- SHA-256 (FIPS 180-4), for cache keys ---- */

struct sha256 {
    uint32_t h[8];
    uint64_t len;
    unsigned char buf[64];
    size_t used;
};

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(struct sha256 *s, const unsigned char *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha_init(struct sha256 *s) {
    static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, init, sizeof init);
    s->len = 0;
    s->used = 0;
}

static void sha_add(struct sha256 *s, const void *data, size_t n) {
    const unsigned char *p = data;
    s->len += n;
    while (n > 0) {
        size_t take = 64 - s->used < n ? 64 - s->used : n;
        memcpy(s->buf + s->used, p, take);
        s->used += take;
        p += take;
        n -= take;
        if (s->used == 64) {
            sha_block(s, s->buf);
            s->used = 0;
        }
    }
}

static void sha_hex(struct sha256 *s, char out[65]) {
    uint64_t bits = s->len * 8;
    unsigned char pad = 0x80, zero = 0, len[8];
    sha_add(s, &pad, 1);
    while (s->used != 56) sha_add(s, &zero, 1);
    for (int i = 0; i < 8; i++) len[i] = (unsigned char)(bits >> (56 - 8 * i));
    sha_add(s, len, 8);
    for (int i = 0; i < 8; i++) snprintf(out + 8 * i, 9, "%08x", s->h[i]);
}

/* ---- the working folder ---- */

struct listing {
    int count;
    char *name[MAX_FILES];
};

static int by_name(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/*
 * The regular files in the current folder, sorted. Returns 0 when there is
 * anything else (a subfolder for a data file named with a path, say) or too
 * much, which only means the run is not cached.
 */
static int list_files(struct listing *out) {
    out->count = 0;
    DIR *dir = opendir(".");
    if (!dir) return 0;
    int plain = 1;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        struct stat st;
        if (lstat(e->d_name, &st) != 0 || !S_ISREG(st.st_mode) || out->count == MAX_FILES) {
            plain = 0;
            continue;
        }
        out->name[out->count++] = strdup(e->d_name);
    }
    closedir(dir);
    qsort(out->name, out->count, sizeof out->name[0], by_name);
    return plain;
}

static int listed(const struct listing *l, const char *name) {
    for (int i = 0; i < l->count; i++) {
        if (strcmp(l->name[i], name) == 0) return 1;
    }
    return 0;
}

static char *read_file(const char *path, long *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *data = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n >= 0 && n <= MAX_INPUT_BYTES && fseek(f, 0, SEEK_SET) == 0) {
            data = malloc(n + 1);
            if (data && fread(data, 1, n, f) != (size_t)n) {
                free(data);
                data = NULL;
            }
            if (data) {
                data[n] = '\0';
                *size = n;
            }
        }
    }
    fclose(f);
    return data;
}

static int write_file(const char *path, const char *data, long size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, size, f) == (size_t)size;
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}

static int copy_file(const char *from, const char *to) {
    long size = 0;
    char *data = read_file(from, &size);
    if (!data) return -1;
    int r = write_file(to, data, size);
    free(data);
    return r;
}

/* The .bcf among the arguments: the one argument naming a file here, with or without .bcf. */
static const char *control_file(int argc, char **argv, const struct listing *files, char *buf, size_t size) {
    const char *found = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        size_t n = strlen(argv[i]);
        const char *name = argv[i];
        if (n < 4 || strcmp(argv[i] + n - 4, ".bcf") != 0) {
            if (snprintf(buf, size, "%s.bcf", argv[i]) >= (int)size) return NULL;
            name = buf;
        }
        if (!listed(files, name) || found) return NULL;
        found = name;
    }
    return found;
}

/*
 * The data sources the .bcf names: [copies] gets those that are plain file
 * names present in the folder (the copies Tectonic made). True when every
 * source is such a file, so nothing biber reads is outside the folder; remote
 * and globbed sources, paths and anything with XML escapes make it false.
 */
static int sources(const char *bcf, const struct listing *files, struct listing *copies) {
    int local = 1, any = 0;
    copies->count = 0;
    for (const char *at = bcf; (at = strstr(at, "<bcf:datasource")) != NULL;) {
        const char *open = strchr(at, '>');
        const char *close = open ? strstr(open, "</bcf:datasource>") : NULL;
        if (!close) return 0;
        size_t attrs = (size_t)(open - at), n = (size_t)(close - open - 1);
        char head[512], name[NAME_MAX + 1];
        if (attrs >= sizeof head || n == 0 || n > NAME_MAX) return 0;
        memcpy(head, at, attrs);
        head[attrs] = '\0';
        memcpy(name, open + 1, n);
        name[n] = '\0';
        int plain = !strchr(name, '/') && !strchr(name, '&') && listed(files, name);
        if (plain && !listed(copies, name) && copies->count < MAX_FILES) copies->name[copies->count++] = strdup(name);
        if (!plain || !strstr(head, "type=\"file\"") || strstr(head, "glob=\"true\"")) local = 0;
        any = 1;
        at = close;
    }
    return local && any;
}

/*
 * The cache key for this run: the launcher's version, the Perl tree's version
 * marker (BIBER_HOME/.version, the shipped archive's SHA-256, which the app
 * writes when it unpacks the tree), the arguments and every file here. 0 when
 * there is no marker: a tree of unknown version is never cached.
 */
static int cache_key(int argc, char **argv, const char *home, const struct listing *files, char key[65]) {
    char marker[PATH_MAX];
    snprintf(marker, sizeof marker, "%s/.version", home);
    long size = 0;
    char *tree = read_file(marker, &size);
    if (!tree || size == 0) {
        free(tree);
        return 0;
    }
    struct sha256 s;
    sha_init(&s);
    sha_add(&s, CACHE_VERSION, sizeof CACHE_VERSION);
    sha_add(&s, tree, size + 1);
    free(tree);
    for (int i = 1; i < argc; i++) sha_add(&s, argv[i], strlen(argv[i]) + 1);
    sha_add(&s, "", 1);
    for (int i = 0; i < files->count; i++) {
        char *data = read_file(files->name[i], &size);
        if (!data) return 0;
        unsigned char len[8];
        for (int b = 0; b < 8; b++) len[b] = (unsigned char)((uint64_t)size >> (56 - 8 * b));
        sha_add(&s, files->name[i], strlen(files->name[i]) + 1);
        sha_add(&s, len, sizeof len);
        sha_add(&s, data, size);
        free(data);
    }
    sha_hex(&s, key);
    return 1;
}

static void remove_tree(const char *path);

/* Copies a cached result into the working folder. 0 on success. */
static int cache_take(const char *cache, const char *key) {
    char entry[PATH_MAX];
    snprintf(entry, sizeof entry, "%s/%s", cache, key);
    DIR *dir = opendir(entry);
    if (!dir) return -1;
    int r = 0, n = 0;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (e->d_name[0] == '.') continue;
        char from[PATH_MAX];
        snprintf(from, sizeof from, "%s/%s", entry, e->d_name);
        if (copy_file(from, e->d_name) != 0) r = -1;
        n++;
    }
    closedir(dir);
    if (r == 0 && n > 0) {
        utimensat(AT_FDCWD, entry, NULL, 0);   /* most recently used */
        return 0;
    }
    /* Empty or unreadable: drop it, or no result could ever be stored under this key. */
    remove_tree(entry);
    return -1;
}

static void remove_tree(const char *path) {
    DIR *dir = opendir(path);
    if (dir) {
        struct dirent *e;
        while ((e = readdir(dir))) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            char child[PATH_MAX];
            snprintf(child, sizeof child, "%s/%s", path, e->d_name);
            unlink(child);
        }
        closedir(dir);
    }
    rmdir(path);
}

/* Keeps the newest CACHE_KEEP results; also clears unfinished ones. */
static void cache_prune(const char *cache) {
    DIR *dir = opendir(cache);
    if (!dir) return;
    char names[64][80];
    time_t times[64];
    int count = 0;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", cache, e->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) continue;
        if (strlen(e->d_name) != 64 || e->d_name[0] == '.') {
            /* A result still being written (.new-PID), or one an interrupted run left: old ones go. */
            if (time(NULL) - st.st_mtime > 600) remove_tree(path);
            continue;
        }
        if (count < 64) {
            snprintf(names[count], sizeof names[count], "%s", e->d_name);
            times[count++] = st.st_mtime;
        }
    }
    closedir(dir);
    while (count > CACHE_KEEP) {
        int oldest = 0;
        for (int i = 1; i < count; i++) if (times[i] < times[oldest]) oldest = i;
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", cache, names[oldest]);
        remove_tree(path);
        names[oldest][0] = '\0';
        times[oldest] = times[--count];
        memcpy(names[oldest], names[count], sizeof names[oldest]);
    }
}

/* mkdir -p: the cache folder may not exist yet (a hidden folder beside the document, say). */
static void make_dirs(const char *path) {
    char buf[PATH_MAX];
    if (snprintf(buf, sizeof buf, "%s", path) >= (int)sizeof buf) return;
    for (char *c = buf + 1; *c; c++) {
        if (*c != '/') continue;
        *c = '\0';
        mkdir(buf, 0700);
        *c = '/';
    }
    mkdir(buf, 0700);
}

/* Keeps the files this run made (not in [before]) as the result for [key]. */
static void cache_put(const char *cache, const char *key, const struct listing *before) {
    struct listing after;
    list_files(&after);
    make_dirs(cache);
    char tmp[PATH_MAX], entry[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s/.new-%d", cache, (int)getpid());
    snprintf(entry, sizeof entry, "%s/%s", cache, key);
    remove_tree(tmp);
    if (mkdir(tmp, 0700) != 0) return;
    int n = 0, ok = 1;
    for (int i = 0; i < after.count; i++) {
        if (listed(before, after.name[i])) continue;
        char to[PATH_MAX];
        snprintf(to, sizeof to, "%s/%s", tmp, after.name[i]);
        if (copy_file(after.name[i], to) != 0) ok = 0;
        n++;
    }
    /* The rename makes a result appear whole or not at all. */
    if (!ok || n == 0 || rename(tmp, entry) != 0) remove_tree(tmp);
    cache_prune(cache);
}

/*
 * True when the current folder is one Tectonic made for its external tool: a
 * folder named .tmp* (Rust's tempfile) directly inside TMPDIR. Nothing is
 * ever deleted anywhere else, such as a document's own folder.
 */
static int in_tectonic_temp(void) {
    const char *tmp = getenv("TMPDIR");
    char cwd[PATH_MAX], real_tmp[PATH_MAX];
    if (!tmp || !getcwd(cwd, sizeof cwd) || !realpath(tmp, real_tmp)) return 0;
    char real_cwd[PATH_MAX];
    if (!realpath(cwd, real_cwd)) return 0;
    char *slash = strrchr(real_cwd, '/');
    if (!slash || strncmp(slash + 1, ".tmp", 4) != 0 || strlen(slash + 1) <= 4) return 0;
    *slash = '\0';
    return strcmp(real_cwd, real_tmp) == 0;
}

/* ---- timing (BIBER_TIMING) ---- */

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

/* Processor time this process has used, in milliseconds. */
static double cpu_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

/* Milliseconds since this process was started (exec included), from /proc; -1 when unknown. */
static double since_start_ms(void) {
    char stat[1024];
    int fd = open("/proc/self/stat", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = read(fd, stat, sizeof stat - 1);
    close(fd);
    if (n <= 0) return -1;
    stat[n] = '\0';
    /* Field 22 (start time, in clock ticks since boot), counted after the command name. */
    char *at = strrchr(stat, ')');
    unsigned long long ticks = 0;
    if (!at || sscanf(at + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %llu", &ticks) != 1)
        return -1;
    struct timespec t;
    if (clock_gettime(CLOCK_BOOTTIME, &t) != 0) return -1;
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6 - ticks * 1e3 / sysconf(_SC_CLK_TCK);
}

/*
 * BIBER_TIMING=1 prints one line on stderr when the run ends; an absolute
 * path appends the line to that file instead.
 */
static void timing_report(const char *line) {
    const char *to = getenv("BIBER_TIMING");
    if (!to || !*to || strcmp(to, "0") == 0) return;
    FILE *f = to[0] == '/' ? fopen(to, "a") : stderr;
    if (!f) return;
    fprintf(f, "biber-timing: %s\n", line);
    if (f != stderr) fclose(f); else fflush(f);
}

/* ---- perl ---- */

struct phase {
    double wall, cpu;
};

static struct phase phase_now(void) {
    struct phase p = {now_ms(), cpu_ms()};
    return p;
}

/* perl -I$HOME/lib -MBiberNative $HOME/bin/biber ARGS..., compiled but not yet run. True when it compiled. */
static int perl_compile(int argc, char **argv, const char *home) {
    static char inc[PATH_MAX], script[PATH_MAX];
    snprintf(inc, sizeof inc, "-I%s/lib", home);
    snprintf(script, sizeof script, "%s/bin/biber", home);
    int nargc = argc + 3;
    char **nargv = calloc(nargc + 1, sizeof *nargv);
    nargv[0] = argv[0];
    nargv[1] = inc;
    nargv[2] = "-MBiberNative";
    nargv[3] = script;
    for (int i = 1; i < argc; i++) nargv[i + 3] = argv[i];

    /* setenv changed environ, not main's env array: give perl the live one. */
    char **env = environ;
    PERL_SYS_INIT3(&nargc, &nargv, &env);
    my_perl = perl_alloc();
    perl_construct(my_perl);
    /*
     * As perlmain.c. perl_construct asks for a full teardown at exit, which
     * frees every value one by one (0.15 s here); the perl program, and so
     * the official biber, runs END blocks and destructors and lets the
     * process exit take the memory.
     */
    PL_perl_destruct_level = 0;
    PL_exit_flags |= PERL_EXIT_DESTRUCT_END;
    return !perl_parse(my_perl, xs_init, nargc, nargv, env);
}

static int run_perl(int argc, char **argv, const char *home) {
    double before_main = since_start_ms();
    struct phase p0 = phase_now();
    int compiled = perl_compile(argc, argv, home);
    struct phase p1 = phase_now();
    if (compiled) perl_run(my_perl);
    struct phase p2 = phase_now();
    int status = perl_destruct(my_perl);
    perl_free(my_perl);
    PERL_SYS_TERM();
    struct phase p3 = phase_now();
    char line[240];
    snprintf(line, sizeof line, "start %.0f ms, compile %.0f ms (cpu %.0f), run %.0f ms (cpu %.0f), exit %.0f ms (cpu %.0f), total %.0f ms",
             before_main, p1.wall - p0.wall, p1.cpu - p0.cpu, p2.wall - p1.wall, p2.cpu - p1.cpu,
             p3.wall - p2.wall, p3.cpu - p2.cpu, before_main + p3.wall - p0.wall);
    timing_report(line);
    return status;
}

/* ---- the warm process (BIBER_WARM) ---- */

/*
 * A run done by the warm process must be the run it would have been on its
 * own. What makes that so:
 *
 *  - The warm process only takes runs started as it was: same launcher file,
 *    uid, Perl tree marker and environment, bar the variables only a run
 *    reads (client_only). Anything loaded through the environment, such as
 *    an LD_PRELOAD library that took its settings from variables when the
 *    process started, was therefore loaded with the run's own settings.
 *  - The worker takes on everything else of the run: descriptors 0 to 2
 *    themselves (so output goes where the run's would, buffered as perl
 *    would for that kind of descriptor), the working folder, arguments,
 *    environment, umask, resource limits, priority, blocked and ignored
 *    signals. The signal handlers perl installed while compiling are the
 *    worker's, never the warm process's.
 *  - The run passes on the signals it gets, and ends with the worker's exit
 *    status or by the signal that ended the worker.
 *  - The worker starts biber only when the run has told it to go, and the
 *    run says so only once the worker is ready. Until then nothing has
 *    happened: a warm process that is missing, slow, refuses or fails leaves
 *    the run to do the work itself. After "go" the work is never done a
 *    second time: if the warm process dies then, the run fails.
 *
 * Not carried over: the session, process group and controlling terminal
 * (signals from a terminal reach the run and are passed on, job control
 * stops are not), and the parent: the worker is a child of the warm process.
 */

#define WARM_MAGIC 0x31575242u   /* "BRW1" */
#define WARM_WAIT_MS 3000        /* a run's patience until the warm process takes it */
#define WARM_REQUEST_MS 2000     /* the warm process's patience with a request */
#define WARM_RUNS 16
#define WARM_REQUEST (256 << 10)
#define WARM_LIMITS 16
#define WARM_SIGNALS 64
#define WARM_REASON 160

/* What a run sends after the head, followed by its strings: environment, then arguments. */
struct warm_run {
    char identity[72];
    uint32_t mask;
    int32_t nice;
    uint64_t blocked, ignored;
    uint64_t limits[WARM_LIMITS][2];
    uint32_t envs, args;
};

struct warm_head {
    uint32_t magic, size;
};

/* Variables only the run's own process reads, or that a shell sets for itself. */
static int client_only(const char *entry) {
    static const char *const names[] = {"BIBER_CACHE=", "BIBER_TIMING=", "BIBER_WARM=", "BIBER_WARM_WAIT=",
                                        "BIBER_WARM_IDLE=", "PWD=", "OLDPWD=", "SHLVL=", "_="};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strncmp(entry, names[i], strlen(names[i])) == 0) return 1;
    }
    return 0;
}

/* This launcher file, the uid and the Perl tree's marker; 0 without a marker. */
static int warm_identity(const char *home, char id[65]) {
    char marker[PATH_MAX];
    snprintf(marker, sizeof marker, "%s/.version", home);
    long size = 0;
    char *tree = read_file(marker, &size);
    if (!tree || size == 0) {
        free(tree);
        return 0;
    }
    struct sha256 s;
    sha_init(&s);
    sha_add(&s, CACHE_VERSION, sizeof CACHE_VERSION);
    sha_add(&s, tree, size + 1);
    free(tree);
    struct stat st;
    if (stat("/proc/self/exe", &st) != 0) return 0;
    uint64_t file[5] = {st.st_dev, st.st_ino, (uint64_t)st.st_size, (uint64_t)st.st_mtim.tv_sec, (uint64_t)st.st_mtim.tv_nsec};
    sha_add(&s, file, sizeof file);
    uint32_t uid = (uint32_t)geteuid();
    sha_add(&s, &uid, sizeof uid);
    sha_hex(&s, id);
    return 1;
}

/* "@name" is a socket in the abstract namespace, "/path" one in the file system. 0 for anything else. */
static socklen_t warm_address(const char *text, struct sockaddr_un *addr) {
    memset(addr, 0, sizeof *addr);
    addr->sun_family = AF_UNIX;
    size_t n = text ? strlen(text) : 0;
    if (n >= 2 && text[0] == '@' && n <= sizeof addr->sun_path) {
        memcpy(addr->sun_path + 1, text + 1, n - 1);
        return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + n);
    }
    if (n >= 2 && text[0] == '/' && n < sizeof addr->sun_path) {
        memcpy(addr->sun_path, text, n + 1);
        return (socklen_t)sizeof *addr;
    }
    return 0;
}

static int same_user(int fd) {
    struct ucred peer;
    socklen_t len = sizeof peer;
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &len) == 0 && peer.uid == geteuid();
}

static int write_all(int fd, const void *data, size_t n) {
    const char *p = data;
    while (n > 0) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* The signals in [set] as bits: signal N is bit N - 1. */
static uint64_t signal_bits(const sigset_t *set) {
    uint64_t bits = 0;
    for (int sig = 1; sig <= WARM_SIGNALS; sig++) {
        if (sigismember(set, sig) == 1) bits |= (uint64_t)1 << (sig - 1);
    }
    return bits;
}

static uint64_t ignored_signals(void) {
    uint64_t bits = 0;
    for (int sig = 1; sig <= WARM_SIGNALS; sig++) {
        struct sigaction sa;
        if (sigaction(sig, NULL, &sa) == 0 && sa.sa_handler == SIG_IGN) bits |= (uint64_t)1 << (sig - 1);
    }
    return bits;
}

/* What this process ignored and blocked when it started. */
static uint64_t ignored_at_start;
static sigset_t blocked_at_start;

/* -- the warm process's side -- */

enum { R_REQUEST, R_SETUP, R_OFFERED, R_RUNNING, R_ABANDONED };

struct served {
    int state;
    int client;             /* the run's socket; -1 once the run is gone */
    int fds[4], nfds;       /* the run's descriptors 0 to 2 and its folder, until the worker has them */
    char *buf;              /* the request: head, then body */
    size_t have;
    double deadline;        /* for the request to be complete */
    pid_t pid;              /* the worker; 0 before there is one */
    int sync;               /* our end of the socket pair with the worker; -1 when closed */
    int half;               /* a 'K' came from the run and its signal number has not yet */
};

static struct served runs[WARM_RUNS];
static int run_count;
static int warm_listener = -1, wake_pipe[2] = {-1, -1};
static volatile sig_atomic_t stop_asked;
static struct sigaction perl_signals[WARM_SIGNALS + 1];
static char **warm_env;     /* our environment without the client-only variables, sorted */
static int warm_envs;
static char warm_id[65];

static void on_wake(int sig) {
    int saved = errno;
    if (sig != SIGCHLD) stop_asked = 1;
    if (write(wake_pipe[1], "w", 1) < 0) { /* full: a wake-up is already waiting */ }
    errno = saved;
}

/* The environment entries that count, sorted. */
static char **counted_env(char **env, int n, int *count) {
    char **out = malloc((n + 1) * sizeof *out);
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (!client_only(env[i])) out[m++] = env[i];
    }
    qsort(out, m, sizeof *out, by_name);
    *count = m;
    return out;
}

static void drop_fds(struct served *r) {
    for (int i = 0; i < r->nfds; i++) close(r->fds[i]);
    r->nfds = 0;
}

static void drop_run(struct served *r) {
    drop_fds(r);
    if (r->client >= 0) close(r->client);
    if (r->sync >= 0) close(r->sync);
    free(r->buf);
    *r = runs[--run_count];
}

/* Tells the run to do the work itself, and why. */
static void refuse(struct served *r, const char *why) {
    char msg[2 + WARM_REASON];
    size_t n = strlen(why) < WARM_REASON ? strlen(why) : WARM_REASON - 1;
    msg[0] = 'N';
    msg[1] = (char)n;
    memcpy(msg + 2, why, n);
    if (r->client >= 0) {
        write_all(r->client, msg, 2 + n);
        close(r->client);
        r->client = -1;
    }
}

static void worker_failed(int sync, const char *what) {
    char msg[WARM_REASON];
    int n = snprintf(msg, sizeof msg, "e%s: %s", what, strerror(errno));
    if (write(sync, msg, n < (int)sizeof msg ? n : (int)sizeof msg - 1) < 0) { /* the warm process is gone */ }
    _exit(0);
}

/* Line buffering as perl sets it when it opens a descriptor: for a terminal only. */
static void adopt_terminal(PerlIO *f, int fd) {
    if (!f || !*f) return;
    int tty = isatty(fd);
    for (PerlIOl *l = *f; l; l = l->next) {
        if (!l->tab || !(l->tab->kind & PERLIO_K_BUFFERED)) continue;
        if (tty) l->flags |= PERLIO_F_LINEBUF | PERLIO_F_TTY;
        else l->flags &= ~(PERLIO_F_LINEBUF | PERLIO_F_TTY);
    }
}

/* In a worker: becomes the run [r] asked for, in the perl the warm process compiled. Does not return. */
static void work(struct served *r, pid_t parent) {
    int sync = r->sync;
    /* A worker never outlives the warm process. */
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() != parent) _exit(0);
    /* Nothing of the warm process or of other runs stays open here. */
    close(warm_listener);
    close(wake_pipe[0]);
    close(wake_pipe[1]);
    for (int i = 0; i < run_count; i++) {
        struct served *o = &runs[i];
        if (o->client >= 0) close(o->client);
        if (o == r) continue;
        if (o->sync >= 0) close(o->sync);
        for (int k = 0; k < o->nfds; k++) close(o->fds[k]);
    }
    struct warm_run *run = (struct warm_run *)(r->buf + sizeof(struct warm_head));
    char *text = (char *)(run + 1);

    for (int i = 0; i < 3; i++) {
        if (dup2(r->fds[i], i) < 0) worker_failed(sync, "dup2");
    }
    if (fchdir(r->fds[3]) != 0) worker_failed(sync, "cannot enter the working folder");
    for (int i = 0; i < 4; i++) close(r->fds[i]);
    umask((mode_t)run->mask);
    for (int i = 0; i < WARM_LIMITS; i++) {
        struct rlimit now, want = {(rlim_t)run->limits[i][0], (rlim_t)run->limits[i][1]};
        if (getrlimit(i, &now) != 0) continue;
        if (now.rlim_cur == want.rlim_cur && now.rlim_max == want.rlim_max) continue;
        if (setrlimit(i, &want) != 0) worker_failed(sync, "setrlimit");
    }
    errno = 0;
    int nice_now = getpriority(PRIO_PROCESS, 0);
    if ((errno != 0 || nice_now != run->nice) && setpriority(PRIO_PROCESS, 0, run->nice) != 0) worker_failed(sync, "setpriority");

    /* What a perl started now would have: its environment, arguments, start time, an unseeded rand. */
    HV *env = get_hv("ENV", GV_ADD);
    hv_clear(env);
    for (uint32_t i = 0; i < run->envs; i++, text += strlen(text) + 1) {
        char *eq = strchr(text, '=');
        if (!eq || eq == text) continue;
        SV *value = newSVpv(eq + 1, 0);
        (void)hv_store(env, text, (I32)(eq - text), value, 0);
        mg_set(value);
    }
    AV *argv = get_av("ARGV", GV_ADD);
    av_clear(argv);
    for (uint32_t i = 0; i < run->args; i++, text += strlen(text) + 1) av_push(argv, newSVpv(text, 0));
    PL_srand_called = FALSE;
    adopt_terminal(PerlIO_stdin(), 0);
    adopt_terminal(PerlIO_stdout(), 1);
    adopt_terminal(PerlIO_stderr(), 2);

    /* Perl's signal handlers back in place, then the signals the run ignores, as perl sees an ignored signal. */
    for (int sig = 1; sig <= WARM_SIGNALS; sig++) {
        if (sig != SIGKILL && sig != SIGSTOP) sigaction(sig, &perl_signals[sig], NULL);
    }
    PL_sig_pending = 0;
    for (int i = 0; PL_sig_name[i]; i++) {
        int sig = PL_sig_num[i];
        if (sig < 1 || sig > WARM_SIGNALS || !(run->ignored >> (sig - 1) & 1)) continue;
        char code[64];
        snprintf(code, sizeof code, "$SIG{%s} = 'IGNORE'", PL_sig_name[i]);
        eval_pv(code, FALSE);
    }

    /* Ready. The run answers "go", or goes away and leaves nothing done. */
    char go = 0;
    ssize_t n;
    if (write(sync, "k", 1) != 1) _exit(0);
    while ((n = read(sync, &go, 1)) < 0 && errno == EINTR) {}
    if (n != 1 || go != 'g') _exit(0);
    close(sync);

    PL_basetime = time(NULL);
#ifdef PERL_HASH_RANDOMIZE_KEYS
    /* The order of a new hash's keys differs from run to run, as between two perls. */
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    PL_hash_rand_bits ^= (UV)t.tv_nsec * 2654435761u ^ (UV)getpid() << 16;
#endif
    sigset_t mask;
    sigemptyset(&mask);
    for (int sig = 1; sig <= WARM_SIGNALS; sig++) {
        if (run->blocked >> (sig - 1) & 1) sigaddset(&mask, sig);
    }
    sigprocmask(SIG_SETMASK, &mask, NULL);
    errno = 0;   /* as perl leaves it when it has compiled the script */
    perl_run(my_perl);
    int status = perl_destruct(my_perl);
    perl_free(my_perl);
    PERL_SYS_TERM();
    exit(status);
}

/* The request is complete: checks it and starts its worker, or refuses. */
static void take_request(struct served *r) {
    struct warm_head *head = (struct warm_head *)r->buf;
    struct warm_run *run = (struct warm_run *)(head + 1);
    char *text = (char *)(run + 1), *end = r->buf + sizeof *head + head->size;
    if (r->nfds != 4 || end[-1] != '\0' || memchr(run->identity, '\0', sizeof run->identity) == NULL) {
        refuse(r, "a request that is not one");
        drop_run(r);
        return;
    }
    uint64_t strings = 0;
    for (char *at = text; at < end; at += strlen(at) + 1) strings++;
    if (strings != (uint64_t)run->envs + run->args) {
        refuse(r, "a request that is not one");
        drop_run(r);
        return;
    }
    if (strcmp(run->identity, warm_id) != 0) {
        refuse(r, "another launcher, user or Perl tree than the warm process's");
        drop_run(r);
        return;
    }
    /* The environment, bar the client-only variables, must be ours. */
    char **theirs = malloc((run->envs + 1) * sizeof *theirs);
    char *at = text;
    for (uint32_t i = 0; i < run->envs; i++, at += strlen(at) + 1) theirs[i] = at;
    int count = 0;
    char **counted = counted_env(theirs, (int)run->envs, &count);
    const char *differs = NULL;
    for (int i = 0; !differs && (i < count || i < warm_envs); i++) {
        if (i >= count) differs = warm_env[i];
        else if (i >= warm_envs) differs = counted[i];
        else if (strcmp(counted[i], warm_env[i]) != 0) differs = strcmp(counted[i], warm_env[i]) < 0 ? counted[i] : warm_env[i];
    }
    char why[WARM_REASON];
    if (differs) snprintf(why, sizeof why, "the environment differs from the warm process's: %.*s", (int)strcspn(differs, "="), differs);
    free(counted);
    free(theirs);
    if (differs) {
        refuse(r, why);
        drop_run(r);
        return;
    }

    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0) {
        refuse(r, "no socket pair for a worker");
        drop_run(r);
        return;
    }
    /* The worker starts with every signal blocked, until it has perl's handlers back. */
    sigset_t all, old;
    sigfillset(&all);
    sigprocmask(SIG_SETMASK, &all, &old);
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid == 0) {
        close(pair[0]);
        r->sync = pair[1];
        work(r, parent);
    }
    sigprocmask(SIG_SETMASK, &old, NULL);
    close(pair[1]);
    if (pid < 0) {
        close(pair[0]);
        refuse(r, "no worker: fork failed");
        drop_run(r);
        return;
    }
    r->pid = pid;
    r->sync = pair[0];
    r->state = R_SETUP;
    /* The worker has the run's descriptors now: whoever waits for them to close must not wait for us. */
    drop_fds(r);
}

/* More of a request arrived. */
static void read_request(struct served *r) {
    size_t head = sizeof(struct warm_head);
    size_t want = r->have < head ? head : head + ((struct warm_head *)r->buf)->size;
    struct iovec iov = {r->buf + r->have, want - r->have};
    union {
        struct cmsghdr header;
        char space[CMSG_SPACE(8 * sizeof(int))];
    } control;
    struct msghdr msg = {0};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.space;
    msg.msg_controllen = sizeof control.space;
    ssize_t n = recvmsg(r->client, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
    int bad = n <= 0 || (msg.msg_flags & MSG_CTRUNC);
    for (struct cmsghdr *c = n > 0 ? CMSG_FIRSTHDR(&msg) : NULL; c; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) continue;
        int count = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
        int *sent = (int *)CMSG_DATA(c);
        for (int i = 0; i < count; i++) {
            if (r->nfds < 4) r->fds[r->nfds++] = sent[i];
            else {
                close(sent[i]);
                bad = 1;
            }
        }
    }
    if (bad) {
        drop_run(r);
        return;
    }
    r->have += (size_t)n;
    if (r->have == head) {
        struct warm_head *h = (struct warm_head *)r->buf;
        if (h->magic != WARM_MAGIC || h->size <= sizeof(struct warm_run) || h->size > WARM_REQUEST) {
            drop_run(r);
            return;
        }
        char *more = realloc(r->buf, head + h->size);
        if (!more) {
            drop_run(r);
            return;
        }
        r->buf = more;
    } else if (r->have > head && r->have == want) {
        take_request(r);
    }
}

/* The worker said how its setup went. */
static void read_sync(struct served *r) {
    char msg[WARM_REASON];
    ssize_t n = recv(r->sync, msg, sizeof msg - 1, MSG_DONTWAIT);
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
    if (n >= 1 && msg[0] == 'k') {
        if (r->client >= 0 && write_all(r->client, "Q", 1) == 0) {
            r->state = R_OFFERED;
            return;
        }
        n = 0;
    }
    if (n >= 1) {
        msg[n] = '\0';
        refuse(r, msg + 1);
    } else {
        refuse(r, "the worker ended before it was ready");
    }
    /* The worker ends on its own, or when our end of the pair closes; it is reaped as any other. */
    close(r->sync);
    r->sync = -1;
    r->state = R_ABANDONED;
}

/* The run sent something, or went away. */
static void read_client(struct served *r) {
    unsigned char msg[32];
    ssize_t n = recv(r->client, msg, sizeof msg, MSG_DONTWAIT);
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
    if (n <= 0) {
        /* Gone: nobody is left to take the result. */
        if (r->pid > 0) kill(r->pid, SIGKILL);
        close(r->client);
        r->client = -1;
        if (r->sync >= 0) close(r->sync);
        r->sync = -1;
        r->state = R_ABANDONED;
        return;
    }
    for (ssize_t i = 0; i < n; i++) {
        if (r->half) {
            r->half = 0;
            if (msg[i] >= 1 && msg[i] <= WARM_SIGNALS && msg[i] != SIGKILL && msg[i] != SIGSTOP) kill(r->pid, msg[i]);
        } else if (msg[i] == 'K') {
            r->half = 1;
        } else if (msg[i] == 'G' && r->state == R_OFFERED) {
            if (write(r->sync, "g", 1) != 1) { /* the worker is gone: its exit says so */ }
            close(r->sync);
            r->sync = -1;
            r->state = R_RUNNING;
        }
    }
}

/* Workers that ended: a run's result goes to the run. */
static void reap(void) {
    for (int i = 0; i < run_count;) {
        struct served *r = &runs[i];
        int state;
        if (r->pid <= 0 || waitpid(r->pid, &state, WNOHANG) != r->pid) {
            i++;
            continue;
        }
        if (r->state == R_SETUP) read_sync(r);
        /* Nothing ran: the run may still do the work itself, also when it has said "go" meanwhile. */
        if (r->state == R_SETUP || r->state == R_OFFERED) refuse(r, "the worker ended before the run began");
        if (r->state == R_RUNNING && r->client >= 0) {
            int32_t result[2] = {WIFSIGNALED(state) ? 1 : 0, WIFSIGNALED(state) ? WTERMSIG(state) : WEXITSTATUS(state)};
            char reply[1 + sizeof result] = {'S'};
            memcpy(reply + 1, result, sizeof result);
            write_all(r->client, reply, sizeof reply);
        }
        drop_run(r);
    }
}

static void stop_listening(const struct sockaddr_un *addr, const struct stat *bound) {
    if (warm_listener < 0) return;
    close(warm_listener);
    warm_listener = -1;
    struct stat st;
    /* Our own socket file only: not one a later process put there. */
    if (addr->sun_path[0] == '/' && lstat(addr->sun_path, &st) == 0 && st.st_dev == bound->st_dev && st.st_ino == bound->st_ino)
        unlink(addr->sun_path);
}

/* The warm process itself: "libbiber.so --biberdroid-warm". */
static int warm_main(char *arg0, const char *home) {
    const char *text = getenv("BIBER_WARM"), *native = getenv("BIBER_NATIVE_DIR");
    struct sockaddr_un addr;
    socklen_t addr_size = warm_address(text, &addr);
    if (!addr_size) {
        fprintf(stderr, "biber: BIBER_WARM must be @NAME or an absolute path of at most %d bytes\n", (int)sizeof addr.sun_path - 1);
        return 2;
    }
    if (home[0] != '/' || !native || native[0] != '/') {
        fprintf(stderr, "biber: a warm process needs absolute paths in BIBER_HOME and BIBER_NATIVE_DIR\n");
        return 2;
    }
    if (!warm_identity(home, warm_id)) {
        fprintf(stderr, "biber: a warm process needs the Perl tree's version marker, %s/.version\n", home);
        return 2;
    }
    /* Perl opens its standard handles on 0 to 2 while compiling: all three must be open. */
    for (int fd = 0; fd < 3; fd++) {
        if (fcntl(fd, F_GETFD) < 0 && open("/dev/null", O_RDWR) != fd) return 2;
    }
    const char *idle_text = getenv("BIBER_WARM_IDLE");
    int idle = idle_text ? atoi(idle_text) : 0;

    warm_listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    struct stat bound = {0};
    /* An address in use is never taken over: its owner may be alive. */
    if (warm_listener < 0 || bind(warm_listener, (struct sockaddr *)&addr, addr_size) != 0 ||
        (addr.sun_path[0] == '/' && (chmod(addr.sun_path, 0600) != 0 || lstat(addr.sun_path, &bound) != 0)) ||
        listen(warm_listener, WARM_RUNS) != 0) {
        fprintf(stderr, "biber: cannot listen at %s: %s\n", text, strerror(errno));
        if (bound.st_ino) unlink(addr.sun_path);
        return 2;
    }
    if (pipe2(wake_pipe, O_CLOEXEC | O_NONBLOCK) != 0) return 2;
    /* Nobody at the other end of our standard input: nothing to compile for. */
    struct pollfd input = {0, POLLIN, 0};
    char byte;
    if (poll(&input, 1, 0) > 0 && (!(input.revents & POLLIN) || read(0, &byte, 1) == 0)) {
        stop_listening(&addr, &bound);
        return 0;
    }

    /*
     * Compiled as a run that ignores no signal compiles, so that biber's
     * "use sigtrap" installs every handler it can; a worker then ignores what
     * its run ignores. Signals wait until our own handlers are in; the ones
     * perl installed are kept for the workers.
     */
    sigset_t all, none;
    sigfillset(&all);
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &all, NULL);
    for (int sig = 1; sig <= WARM_SIGNALS; sig++) {
        if (ignored_at_start >> (sig - 1) & 1) signal(sig, SIG_DFL);
    }
    int n = 0;
    while (environ[n]) n++;
    char **copy = malloc((n + 1) * sizeof *copy);
    for (int i = 0; i < n; i++) copy[i] = strdup(environ[i]);
    warm_env = counted_env(copy, n, &warm_envs);
    free(copy);

    double t0 = now_ms();
    char *argv[] = {arg0, NULL};
    int compiled = perl_compile(1, argv, home);
    if (compiled) PerlIO_flush(NULL);
    for (int sig = 1; sig <= WARM_SIGNALS; sig++) sigaction(sig, NULL, &perl_signals[sig]);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_wake;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
    sigprocmask(SIG_SETMASK, &none, NULL);
    if (!compiled) {
        stop_listening(&addr, &bound);
        return 1;
    }
    dprintf(1, "biber: warm process ready at %s, compiled in %.0f ms\n", text, now_ms() - t0);

    int watch_input = 1, stopping = 0, busy = 0;
    double idle_since = now_ms();
    for (;;) {
        if (stop_asked) stopping = 1;
        if (run_count > 0) busy = 1;
        else if (busy) {
            busy = 0;
            idle_since = now_ms();
        }
        if (stopping) {
            /* No new runs; requests not yet taken go back to their runs, those under way finish. */
            stop_listening(&addr, &bound);
            for (int i = 0; i < run_count;) {
                if (runs[i].state == R_REQUEST) drop_run(&runs[i]); else i++;
            }
            if (run_count == 0) break;
        }
        struct pollfd fds[3 + 2 * WARM_RUNS];
        struct served *owner[3 + 2 * WARM_RUNS];
        int count = 0, input_at = -1, listener_at = -1;
        double now = now_ms(), until = -1;
        fds[count].fd = wake_pipe[0]; fds[count++].events = POLLIN;
        if (watch_input) { input_at = count; fds[count].fd = 0; fds[count++].events = POLLIN; }
        if (warm_listener >= 0 && run_count < WARM_RUNS) { listener_at = count; fds[count].fd = warm_listener; fds[count++].events = POLLIN; }
        int fixed = count;
        for (int i = 0; i < run_count; i++) {
            struct served *r = &runs[i];
            if (r->client >= 0) { owner[count] = r; fds[count].fd = r->client; fds[count++].events = POLLIN; }
            if (r->state == R_SETUP) { owner[count] = r; fds[count].fd = r->sync; fds[count++].events = POLLIN; }
            if (r->state == R_REQUEST && (until < 0 || r->deadline < until)) until = r->deadline;
        }
        if (run_count == 0 && idle > 0 && !stopping) until = idle_since + idle * 1e3;
        int ready = poll(fds, count, until < 0 ? -1 : until <= now ? 0 : (int)(until - now) + 1);
        if (ready < 0 && errno != EINTR) break;
        now = now_ms();
        if (ready <= 0) {
            if (run_count == 0 && idle > 0 && now >= idle_since + idle * 1e3) stopping = 1;
            for (int i = 0; i < run_count;) {
                if (runs[i].state == R_REQUEST && now >= runs[i].deadline) drop_run(&runs[i]); else i++;
            }
            continue;
        }
        if (fds[0].revents) {
            char drain[64];
            while (read(wake_pipe[0], drain, sizeof drain) > 0) {}
        }
        /* Our standard input at its end: whoever started us is gone, or wants us gone. */
        if (input_at >= 0 && fds[input_at].revents) {
            char drain[256];
            ssize_t got = fds[input_at].revents & POLLIN ? read(0, drain, sizeof drain) : 0;
            if (got == 0 || (got < 0 && errno != EINTR && errno != EAGAIN)) {
                watch_input = 0;
                stopping = 1;
            }
        }
        /*
         * One event for each run at most, and none for a run that was moved
         * in the table by an earlier one this round: it is seen next round.
         */
        int before = run_count;
        for (int i = fixed; i < count && run_count == before; i++) {
            if (!fds[i].revents) continue;
            struct served *r = owner[i];
            if (fds[i].fd == r->client && r->state == R_REQUEST) read_request(r);
            else if (fds[i].fd == r->client) read_client(r);
            else if (r->state == R_SETUP && fds[i].fd == r->sync) read_sync(r);
            if (i + 1 < count && owner[i + 1] == r) i++;
        }
        reap();
        if (listener_at >= 0 && fds[listener_at].revents && warm_listener >= 0 && run_count < WARM_RUNS) {
            int client = accept4(warm_listener, NULL, NULL, SOCK_CLOEXEC);
            if (client >= 0 && !same_user(client)) {
                close(client);
                client = -1;
            }
            if (client >= 0) {
                struct served *r = &runs[run_count++];
                memset(r, 0, sizeof *r);
                r->state = R_REQUEST;
                r->client = client;
                r->sync = -1;
                r->buf = malloc(sizeof(struct warm_head));
                r->deadline = now + WARM_REQUEST_MS;
            }
        }
    }
    stop_listening(&addr, &bound);
    /* The compiled perl is dropped with the process: nothing ran in it. */
    return 0;
}

/* -- the run's side -- */

static int forward_to = -1;

static void on_forward(int sig) {
    int saved = errno;
    unsigned char msg[2] = {'K', (unsigned char)sig};
    if (send(forward_to, msg, sizeof msg, MSG_NOSIGNAL | MSG_DONTWAIT) < 0) { /* the warm process is gone: the run ends anyway */ }
    errno = saved;
}

/* Reads [n] bytes before [deadline] (in now_ms time; negative: no limit). */
static int read_until(int fd, void *data, size_t n, double deadline) {
    char *p = data;
    while (n > 0) {
        if (deadline >= 0) {
            double left = deadline - now_ms();
            struct pollfd one = {fd, POLLIN, 0};
            int ready = left > 0 ? poll(&one, 1, (int)left + 1) : 0;
            if (ready < 0 && errno == EINTR) continue;
            if (ready <= 0) {
                errno = ETIMEDOUT;
                return -1;
            }
        }
        ssize_t r = read(fd, p, n);
        if (r < 0 && errno == EINTR) continue;
        if (r == 0) errno = ECONNRESET;
        if (r <= 0) return -1;
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

/* Ends this process as the worker ended: by [sig]. */
static void end_by_signal(int sig) {
    sigset_t one;
    sigemptyset(&one);
    sigaddset(&one, sig);
    signal(sig, SIG_DFL);
    sigprocmask(SIG_UNBLOCK, &one, NULL);
    raise(sig);
    _exit(128 + sig);
}

/*
 * Hands this run to the warm process at [address]. 0 with biber's exit
 * status in [status]; -1 when the run has to be done here, with the reason
 * in [why]. Once the warm process has been told to go, this never returns
 * -1: the run is done there, or it failed.
 */
static int warm_run(const char *address, int argc, char **argv, const char *home, uint64_t ignored,
                    const sigset_t *blocked, int *status, char *why, size_t why_size) {
    struct sockaddr_un addr;
    socklen_t addr_size = warm_address(address, &addr);
    struct warm_run run;
    memset(&run, 0, sizeof run);
    const char *native = getenv("BIBER_NATIVE_DIR");
    if (!addr_size || home[0] != '/' || !native || native[0] != '/' || !warm_identity(home, run.identity)) {
        snprintf(why, why_size, "no usable address, absolute BIBER_HOME and BIBER_NATIVE_DIR, or version marker");
        return -1;
    }
    double t0 = now_ms();
    const char *wait_text = getenv("BIBER_WARM_WAIT");
    int patience = wait_text && atoi(wait_text) > 0 ? atoi(wait_text) : WARM_WAIT_MS;
    double deadline = t0 + patience;

    mode_t mask = umask(0);
    umask(mask);
    run.mask = (uint32_t)mask;
    errno = 0;
    run.nice = getpriority(PRIO_PROCESS, 0);
    run.blocked = signal_bits(blocked);
    run.ignored = ignored;
    for (int i = 0; i < WARM_LIMITS; i++) {
        struct rlimit limit;
        if (getrlimit(i, &limit) != 0) limit.rlim_cur = limit.rlim_max = RLIM_INFINITY;
        run.limits[i][0] = (uint64_t)limit.rlim_cur;
        run.limits[i][1] = (uint64_t)limit.rlim_max;
    }
    size_t size = sizeof run;
    for (char **e = environ; *e; e++, run.envs++) size += strlen(*e) + 1;
    for (int i = 1; i < argc; i++, run.args++) size += strlen(argv[i]) + 1;
    if (size > WARM_REQUEST) {
        snprintf(why, why_size, "the environment and arguments are too large to hand over");
        return -1;
    }
    if (fcntl(0, F_GETFD) < 0 || fcntl(1, F_GETFD) < 0 || fcntl(2, F_GETFD) < 0) {
        snprintf(why, why_size, "a standard descriptor is closed");
        return -1;
    }
    int fds[4] = {0, 1, 2, open(".", O_PATH | O_DIRECTORY | O_CLOEXEC)};
    if (fds[3] < 0) {
        snprintf(why, why_size, "the working folder cannot be opened (%s)", strerror(errno));
        return -1;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, addr_size) != 0) {
        snprintf(why, why_size, "no warm process at %s (%s)", address, strerror(errno));
        if (fd >= 0) close(fd);
        close(fds[3]);
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    if (!same_user(fd)) {
        snprintf(why, why_size, "the process at %s is not this user's", address);
        close(fd);
        close(fds[3]);
        return -1;
    }
    struct timeval limit = {patience / 1000, patience % 1000 * 1000};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof limit);

    struct warm_head head = {WARM_MAGIC, (uint32_t)size};
    char *body = malloc(size), *at = body + sizeof run;
    memcpy(body, &run, sizeof run);
    for (char **e = environ; *e; e++) at = stpcpy(at, *e) + 1;
    for (int i = 1; i < argc; i++) at = stpcpy(at, argv[i]) + 1;
    struct iovec iov = {&head, sizeof head};
    union {
        struct cmsghdr header;
        char space[CMSG_SPACE(4 * sizeof(int))];
    } control;
    memset(&control, 0, sizeof control);
    struct msghdr msg = {0};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.space;
    msg.msg_controllen = sizeof control.space;
    struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof fds);
    memcpy(CMSG_DATA(c), fds, sizeof fds);
    int sent = sendmsg(fd, &msg, MSG_NOSIGNAL) == (ssize_t)sizeof head && write_all(fd, body, size) == 0;
    free(body);
    close(fds[3]);
    char reply = 0;
    if (!sent || read_until(fd, &reply, 1, deadline) != 0) {
        snprintf(why, why_size, "the warm process did not take the run%s (%s)", sent ? " in time" : "", strerror(errno));
        close(fd);
        return -1;
    }
    if (reply != 'Q') {
        unsigned char n = 0;
        char reason[256] = "";
        if (reply == 'N' && read_until(fd, &n, 1, deadline) == 0 && n > 0 && read_until(fd, reason, n, deadline) == 0) reason[n] = '\0';
        snprintf(why, why_size, "refused: %s", reason[0] ? reason : "no reason given");
        close(fd);
        return -1;
    }
    double t1 = now_ms();

    /* From here the run is the worker's. Signals for this process go to it. */
    forward_to = fd;
    static const int passed[] = {SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGPIPE, SIGALRM, SIGUSR1, SIGUSR2};
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_forward;
    sa.sa_flags = SA_RESTART;
    sigfillset(&sa.sa_mask);
    for (size_t i = 0; i < sizeof passed / sizeof passed[0]; i++) {
        if (!(ignored >> (passed[i] - 1) & 1)) sigaction(passed[i], &sa, NULL);
    }
    int32_t result[2];
    int told = write_all(fd, "G", 1) == 0 && read_until(fd, &reply, 1, -1) == 0;
    if (told && reply == 'N') {
        /* The worker ended before it was told to go: nothing was done, so the run is done here. */
        for (size_t i = 0; i < sizeof passed / sizeof passed[0]; i++) {
            if (!(ignored >> (passed[i] - 1) & 1)) signal(passed[i], SIG_DFL);
        }
        snprintf(why, why_size, "the worker ended before the run began");
        close(fd);
        return -1;
    }
    if (!told || reply != 'S' || read_until(fd, result, sizeof result, -1) != 0) {
        /* The worker died with the warm process. What it did so far is not done again. */
        dprintf(2, "biber: the warm process ended during this run\n");
        end_by_signal(SIGKILL);
    }
    for (size_t i = 0; i < sizeof passed / sizeof passed[0]; i++) {
        if (!(ignored >> (passed[i] - 1) & 1)) signal(passed[i], SIG_DFL);
    }
    close(fd);
    if (result[0]) end_by_signal(result[1]);
    *status = result[1];
    char line[200];
    double start = since_start_ms() - (now_ms() - t0);
    snprintf(line, sizeof line, "start %.0f ms, warm: taken after %.0f ms, run %.0f ms, total %.0f ms", start, t1 - t0,
             now_ms() - t1, start + now_ms() - t0);
    timing_report(line);
    return 0;
}

/* biber, by the warm process when BIBER_WARM names one that can take this run, or else here. */
static int run_biber(int argc, char **argv, const char *home) {
    const char *address = getenv("BIBER_WARM");
    if (address && *address) {
        int status;
        char why[320], line[360];
        if (warm_run(address, argc, argv, home, ignored_at_start, &blocked_at_start, &status, why, sizeof why) == 0) return status;
        snprintf(line, sizeof line, "warm process not used: %s", why);
        timing_report(line);
    }
    return run_perl(argc, argv, home);
}

int main(int argc, char **argv) {
    ignored_at_start = ignored_signals();
    sigprocmask(SIG_SETMASK, NULL, &blocked_at_start);
    char exe[PATH_MAX];
    if (!getenv("BIBER_NATIVE_DIR")) {
        ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n > 0) {
            exe[n] = '\0';
            char *dir = parent_dir(exe);
            if (dir) setenv("BIBER_NATIVE_DIR", dir, 1);
        }
    }
    const char *home = getenv("BIBER_HOME");
    char *self = home ? NULL : self_path(argv[0]);
    if (self) {
        char *bin = parent_dir(self);
        home = bin ? parent_dir(bin) : NULL;
        if (home) setenv("BIBER_HOME", home, 1);
    }
    if (!home) {
        fprintf(stderr, "biber: BIBER_HOME is not set\n");
        return 2;
    }

    const char *base = strrchr(argv[0], '/');
    base = base ? base + 1 : argv[0];
    if (argc == 2 && strcmp(argv[1], "--biberdroid-warm") == 0) return warm_main(argv[0], home);
    if (strcmp(base, "tectonic-biber") != 0) return run_biber(argc, argv, home);

    /* Tectonic's external tool, in its temporary folder (see the top of the file). */
    struct listing inputs, copies;
    copies.count = 0;
    int plain = list_files(&inputs);
    char buf[PATH_MAX];
    const char *bcf = control_file(argc, argv, &inputs, buf, sizeof buf);
    long size = 0;
    char *text = bcf ? read_file(bcf, &size) : NULL;
    int local = text && sources(text, &inputs, &copies);
    free(text);
    const char *cache = getenv("BIBER_CACHE");
    char key[65];
    int cached = cache && cache[0] == '/' && plain && local && cache_key(argc, argv, home, &inputs, key);
    int status;
    if (cached && cache_take(cache, key) == 0) {
        printf("biber: same input as an earlier run, its result reused\n");
        status = 0;
    } else {
        status = run_biber(argc, argv, home);
        if (status == 0 && cached) cache_put(cache, key, &inputs);
    }
    /* Only Tectonic's copies of the data files, and only in Tectonic's own temporary folder. */
    if (in_tectonic_temp()) {
        for (int i = 0; i < copies.count; i++) unlink(copies.name[i]);
    }
    return status;
}
