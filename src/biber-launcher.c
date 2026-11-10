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
 */
#include <EXTERN.h>
#include <perl.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Part of every cache key: change it when biber or this launcher changes. */
#define CACHE_VERSION "biber 2.21 launcher 3"
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

static int run_perl(int argc, char **argv, const char *home) {
    char inc[PATH_MAX], script[PATH_MAX];
    snprintf(inc, sizeof inc, "-I%s/lib", home);
    snprintf(script, sizeof script, "%s/bin/biber", home);

    /* perl -I$HOME/lib -MBiberNative $HOME/bin/biber ARGS... */
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
    PL_exit_flags |= PERL_EXIT_DESTRUCT_END;
    /* As perlmain.c: perl_destruct gives the exit status, including exit(N). */
    if (!perl_parse(my_perl, xs_init, nargc, nargv, env)) perl_run(my_perl);
    int status = perl_destruct(my_perl);
    perl_free(my_perl);
    PERL_SYS_TERM();
    return status;
}

int main(int argc, char **argv) {
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
    if (strcmp(base, "tectonic-biber") != 0) return run_perl(argc, argv, home);

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
        status = run_perl(argc, argv, home);
        if (status == 0 && cached) cache_put(cache, key, &inputs);
    }
    /* Only Tectonic's copies of the data files, and only in Tectonic's own temporary folder. */
    if (in_tectonic_temp()) {
        for (int i = 0; i < copies.count; i++) unlink(copies.name[i]);
    }
    return status;
}
