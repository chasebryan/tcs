/* Experimental trusted-host workflow. No network, serial, import, or retry API. */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/acl.h>
#elif defined(__linux__)
#include <sys/xattr.h>
#else
#error "Operator file handling supports only local Linux/macOS filesystems"
#endif
#include "tcs/admin.h"
#include "tcs/launch.h"
#include "monocypher-ed25519.h"

#ifdef TCS_OPERATOR_TEST_ONLY
#define MODE TCS_FIXTURE_MODE
extern int tcs_operator_test_entropy(void *, size_t);
extern ssize_t tcs_operator_test_write(int, const void *, size_t);
extern int tcs_operator_test_fsync(int);
#define entropy tcs_operator_test_entropy
#define op_write tcs_operator_test_write
#define op_fsync tcs_operator_test_fsync
#else
#define MODE TCS_OPERATOR_MODE
#define entropy getentropy
#define op_write write
#define op_fsync fsync
#endif

static const uint8_t key_prefix[16] = {'T','C','S','-','K','E','Y',1};
static const int dir_flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;

static bool no_acl(int fd)
{
#ifdef __APPLE__
    acl_t acl = acl_get_fd_np(fd, ACL_TYPE_EXTENDED);
    if (!acl) return errno == ENOENT; /* Darwin: no extended ACL exists. */
    acl_entry_t entry;
    bool ok = acl_valid(acl) == 0;
    if (ok) { errno = 0; ok = acl_get_entry(acl, ACL_FIRST_ENTRY, &entry) == -1 && errno == EINVAL; }
    acl_free(acl); return ok;
#else
    const char *names[] = {"system.posix_acl_access", "system.posix_acl_default"};
    for (unsigned i = 0; i < 2; ++i) {
        errno = 0;
        if (fgetxattr(fd, names[i], NULL, 0) >= 0 || (errno != ENODATA && errno != ENOTSUP)) return false;
    }
    return true;
#endif
}
static bool private_dir(int fd)
{
    struct stat s;
    return fstat(fd, &s) == 0 && S_ISDIR(s.st_mode) && s.st_uid == getuid() &&
        (s.st_mode & 07777) == 0700 && no_acl(fd);
}
static bool outside_repository(int fd)
{
    struct stat s;
    return fstatat(fd, ".git", &s, AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT;
}
/* Walk every component without following symlinks. Keep descriptor-relative
 * ownership even if an ancestor is renamed. No '..', overwrite, or cleanup. */
static int directory(const char *path, bool create)
{
    char copy[4096]; size_t n = strnlen(path, sizeof copy);
    if (!n || n >= sizeof copy || path[0] != '/' || path[n-1] == '/' || strstr(path, "//")) return -1;
    memcpy(copy, path, n + 1);
    int fd = open("/", dir_flags);
    if (fd < 0) return -1;
    char *state = NULL, *part = strtok_r(copy, "/", &state);
    while (part) {
        char *next = strtok_r(NULL, "/", &state);
        if (!strcmp(part, ".") || !strcmp(part, "..") || !outside_repository(fd)) { close(fd); return -1; }
        if (!next && create && (mkdirat(fd, part, 0700) || op_fsync(fd))) { close(fd); return -1; }
        int child = openat(fd, part, dir_flags); close(fd);
        if (child < 0) return -1;
        fd = child; part = next;
    }
    if (!private_dir(fd) || !outside_repository(fd)) { close(fd); return -1; }
    return fd;
}
static int read_file_open(int dir, const char *name, uint8_t *bytes, size_t length)
{
    int fd = openat(dir, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return -1;
    struct stat s; bool ok = fstat(fd, &s) == 0 && S_ISREG(s.st_mode) &&
        s.st_uid == getuid() && s.st_nlink == 1 && (s.st_mode & 07777) == 0600 &&
        s.st_size == (off_t)length && no_acl(fd);
    size_t done = 0;
    while (ok && done < length) {
        ssize_t n = read(fd, bytes + done, length - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) ok = false; else done += (size_t)n;
    }
    uint8_t extra;
    if (ok) { ssize_t n; do { n = read(fd, &extra, 1); } while (n < 0 && errno == EINTR); ok = n == 0; }
    if (ok && lseek(fd, 0, SEEK_SET) != 0) ok = false;
    if (!ok) { close(fd); crypto_wipe(bytes, length); return -1; }
    return fd;
}
static bool read_file(int dir, const char *name, uint8_t *bytes, size_t length)
{
    int fd = read_file_open(dir, name, bytes, length);
    if (fd < 0) return false;
    if (close(fd)) { crypto_wipe(bytes, length); return false; }
    return true;
}
static bool write_new(int dir, const char *name, const uint8_t *bytes, size_t length)
{
    int fd = openat(dir, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    bool ok = no_acl(fd); size_t done = 0;
    while (ok && done < length) {
        ssize_t n = op_write(fd, bytes + done, length - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; }
        done += (size_t)n;
    }
    if (op_fsync(fd)) ok = false;
    if (close(fd)) ok = false;
    if (op_fsync(dir)) ok = false;
    /* An uncertain/partial output stays present and blocks replacement. */
    return ok;
}
static void hex(const uint8_t *bytes, size_t size, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) { out[2*i] = digits[bytes[i] >> 4]; out[2*i+1] = digits[bytes[i] & 15]; }
    out[2*size] = 0;
}
static void display(const char *name, const uint8_t bytes[32])
{ char text[65]; hex(bytes, 32, text); printf("%s=%s\n", name, text); }
static bool identity_read(int fd, struct tcs_identity *identity)
{
    uint8_t bytes[80];
    return read_file(fd, "identity.pub", bytes, sizeof bytes) &&
        tcs_public_decode(bytes, sizeof bytes, MODE, identity);
}
static bool key_read(int fd, struct tcs_identity identity, uint8_t secret[64])
{
    uint8_t record[80] = {0}, seed[32] = {0}, key[32]; bool ok = false;
    if (!read_file(fd, "identity.key", record, sizeof record) || record[15] != MODE ||
        memcmp(record, key_prefix, 15) || memcmp(record + 16, identity.realm, 32)) goto done;
    memcpy(seed, record + 48, 32);
    crypto_ed25519_key_pair(secret, key, seed); /* Derive public half; never import a bundle. */
    ok = memcmp(key, identity.public_key, 32) == 0;
done:
    crypto_wipe(record, sizeof record); crypto_wipe(seed, sizeof seed);
    if (!ok) crypto_wipe(secret, 64);
    return ok;
}
static bool create_identity(const char *path)
{
    uint8_t random[64] = {0}, record[80] = {0}, public[80], secret[64] = {0};
    struct tcs_identity identity; int fd = -1; bool ok = false;
    if (entropy(random, sizeof random)) goto done;
    memcpy(identity.realm, random + 32, 32); memcpy(record, key_prefix, 16); record[15] = MODE;
    memcpy(record + 16, identity.realm, 32); memcpy(record + 48, random, 32);
    crypto_ed25519_key_pair(secret, identity.public_key, random);
    if (!tcs_public_encode(public, identity, MODE)) goto done;
    fd = directory(path, true);
    if (fd < 0) goto done;
    ok = write_new(fd, "identity.key", record, sizeof record) &&
        write_new(fd, "identity.pub", public, sizeof public);
    if (ok) puts("Identity created; private seed is unencrypted. No guest was provisioned.");
done:
    if (fd >= 0) close(fd);
    crypto_wipe(random, sizeof random); crypto_wipe(record, sizeof record); crypto_wipe(secret, sizeof secret);
    return ok;
}
static bool context_create(int identity_fd, const char *path)
{
    struct tcs_launch launch; uint8_t bytes[112];
    if (!identity_read(identity_fd, &launch.identity) || entropy(launch.boot, sizeof launch.boot) ||
        !tcs_launch_encode(bytes, launch, MODE)) return false;
    int fd = directory(path, true);
    if (fd < 0) return false;
    bool ok = write_new(fd, "launch.context", bytes, sizeof bytes); close(fd);
    if (ok) puts("Single-launch context prepared. No guest was started; never reuse this context.");
    return ok;
}
static bool number(const char *text, uint64_t *value)
{
    *value = 0;
    if (!*text || (text[0] == '0' && text[1])) return false;
    for (size_t i = 0; text[i]; ++i) {
        if (i == 20 || text[i] < '0' || text[i] > '9') return false;
        uint64_t digit = (uint64_t)(text[i] - '0');
        if (*value > (UINT64_MAX - digit) / 10) return false;
        *value = *value * 10 + digit;
    }
    return true;
}
static bool command_parse(char **args, struct tcs_admin_command *c)
{
    *c = (struct tcs_admin_command){0};
    if (!strcmp(args[0], "grant")) { c->request.op = TCS_GRANT; c->request.object = TCS_OBJECT; c->request.rights = TCS_READ; }
    else if (!strcmp(args[0], "revoke")) c->request.op = TCS_REVOKE;
    else if (!strcmp(args[0], "quarantine")) c->request.op = TCS_QUARANTINE;
    else if (!strcmp(args[0], "restore")) c->request.op = TCS_RESTORE;
    else return false;
    return number(args[1], &c->request.subject) && c->request.subject < TCS_SUBJECTS &&
        number(args[2], &c->sequence) && c->sequence && number(args[3], &c->expected_generation);
}
static bool prepare(int identity_fd, int session_fd, struct tcs_admin_command c,
    struct tcs_launch *launch, uint8_t packet[192], char approval[65])
{
    struct tcs_identity identity; uint8_t context[112], review[176] = {'T','C','S','-','R','E','V','I','E','W',1}, digest[32];
    if (!identity_read(identity_fd, &identity) || !read_file(session_fd, "launch.context", context, sizeof context) ||
        !tcs_launch_decode(context, sizeof context, MODE, launch) ||
        memcmp(identity.realm, launch->identity.realm, 32) || memcmp(identity.public_key, launch->identity.public_key, 32) ||
        !tcs_admin_encode(packet, identity.realm, launch->boot, c)) return false;
    review[15] = MODE; memcpy(review + 16, identity.public_key, 32); memcpy(review + 48, packet, 128);
    crypto_blake2b(digest, sizeof digest, review, sizeof review); hex(digest, sizeof digest, approval);
    return true;
}
static bool review_or_sign(int identity_fd, const char *path, char **args, const char *approval)
{
    struct tcs_admin_command c; struct tcs_launch launch;
    uint8_t packet[192] = {0}, secret[64] = {0}; char wanted[65]; bool ok = false;
    if (!command_parse(args, &c)) return false;
    int session_fd = directory(path, false);
    if (session_fd < 0) return false;
    if (!prepare(identity_fd, session_fd, c, &launch, packet, wanted)) goto done;
    if (!approval) {
        printf("mode=%s\n", MODE == TCS_FIXTURE_MODE ? "PUBLIC TEST FIXTURE" : "EXPERIMENTAL OPERATOR");
        display("realm", launch.identity.realm); display("public_key", launch.identity.public_key); display("boot", launch.boot);
        printf("operation=%s subject=%" PRIu64 " sequence=%" PRIu64 " expected_generation=%" PRIu64 " object=%" PRIu64 " rights=%" PRIu64 "\n",
            args[0], c.request.subject, c.sequence, c.expected_generation, c.request.object, c.request.rights);
        printf("approval=%s\n", wanted); ok = true; goto done;
    }
    /* Public digest comparison. Approval is local intent, not authentication. */
    if (strcmp(approval, wanted) || !key_read(identity_fd, launch.identity, secret)) goto done;
    crypto_ed25519_sign(packet + 128, secret, packet, 128);
    if (crypto_ed25519_check(packet + 128, launch.identity.public_key, packet, 128)) goto done;
    char name[64]; int length = snprintf(name, sizeof name, "request-%" PRIu64 ".bin", c.sequence);
    if (length < 0 || (size_t)length >= sizeof name) goto done;
    ok = write_new(session_fd, name, packet, sizeof packet);
    if (ok) printf("Signed %s; not submitted or executed. No automatic retry.\n", name);
done:
    crypto_wipe(secret, sizeof secret); close(session_fd); return ok;
}
static bool absolute_argument(const char *path)
{
    if (path[0] != '/') return false;
    for (size_t i = 0; path[i]; ++i)
        if (i >= 4095 || (unsigned char)path[i] < 32 || path[i] == ',' || path[i] == 127) return false;
    return true;
}
static bool close_other_descriptors(void)
{
    DIR *directory = opendir("/dev/fd");
    if (!directory) return false;
    int scanner = dirfd(directory); bool ok = true;
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(directory))) {
        uint64_t value;
        if (number(entry->d_name, &value) && value <= INT32_MAX) {
            int fd = (int)value;
            if (fd > 2 && fd != scanner && close(fd) && errno != EBADF) ok = false;
        }
        errno = 0;
    }
    if (errno) ok = false;
    if (closedir(directory)) ok = false;
    return ok;
}
static bool launch_guest(int identity_fd, const char *session, const char *image, const char *qemu)
{
    if (!absolute_argument(image) || !absolute_argument(qemu)) return false;
    int dir = directory(session, false), image_fd = -1;
    if (dir < 0) return false;
    uint8_t context[112], *image_bytes = NULL; struct tcs_launch launch; struct tcs_identity identity;
    bool valid = identity_read(identity_fd, &identity) && read_file(dir, "launch.context", context, sizeof context) &&
        tcs_launch_decode(context, sizeof context, MODE, &launch) &&
        !memcmp(identity.realm, launch.identity.realm, 32) && !memcmp(identity.public_key, launch.identity.public_key, 32);
    if (!valid) goto done;
    image_fd = open(image, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    struct stat image_stat;
    if (image_fd < 0 || fstat(image_fd, &image_stat) || !S_ISREG(image_stat.st_mode) ||
        image_stat.st_size <= 0 || image_stat.st_size > 64*1024*1024) goto done;
    /* Irreversibly consume the session BEFORE exec; failures never unreserve. */
    if (!write_new(dir, "launch.used", context, sizeof context)) goto done;
    /* QEMU opens image paths repeatedly. Darwin /dev/fd shares offsets,
     * including the loader's seek-to-end size probe. Use exclusive private
     * copies in a descriptor-pinned working directory on both host systems. */
    size_t size = (size_t)image_stat.st_size, done = 0;
    image_bytes = malloc(size);
    if (!image_bytes) goto done;
    while (done < size) {
        ssize_t n = read(image_fd, image_bytes + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto done;
        done += (size_t)n;
    }
    uint8_t extra; ssize_t n;
    do { n = read(image_fd, &extra, 1); } while (n < 0 && errno == EINTR);
    if (n != 0 || !write_new(dir, "launch.img", image_bytes, size)) goto done;
    free(image_bytes); image_bytes = NULL;
    if (fchdir(dir)) goto done;
    char loader[] = "loader,file=launch.img,addr=0x70000000,cpu-num=0,force-raw=on";
    char firmware[] = "name=opt/tcs/launch-context,file=launch.used";
    char *const arguments[] = {(char *)qemu, "-machine", "virt,virtualization=on", "-cpu", "cortex-a53",
        "-m", "2G", "-smp", "1", "-display", "none", "-no-reboot",
        "-chardev", "stdio,id=uart,signal=off", "-serial", "chardev:uart", "-monitor", "none",
        "-nic", "none", "-accel", "tcg", "-global", "fw_cfg_mem.dma_enabled=off",
        "-device", loader, "-fw_cfg", firmware, NULL};
    if (!close_other_descriptors()) goto done;
    execv(qemu, arguments);
done:
    free(image_bytes);
    if (image_fd >= 0) close(image_fd);
    close(dir); return false;
}
static int usage(void)
{
    fputs("Experimental TCS host tool (no automatic request submission):\n"
        "  tcs-operator create NEW_IDENTITY_DIR --acknowledge-experimental\n"
        "  tcs-operator show IDENTITY_DIR\n"
        "  tcs-operator context IDENTITY_DIR NEW_SESSION_DIR --acknowledge-experimental\n"
        "  tcs-operator review IDENTITY_DIR SESSION_DIR OP SUBJECT SEQUENCE GENERATION\n"
        "  tcs-operator sign IDENTITY_DIR SESSION_DIR OP SUBJECT SEQUENCE GENERATION APPROVAL\n"
        "  tcs-operator launch IDENTITY_DIR SESSION_DIR IMAGE QEMU --acknowledge-experimental\n"
        "OP: grant | revoke | quarantine | restore. Use canonical decimal numbers.\n"
        "Use absolute real paths outside Git checkouts; no symlink components.\n"
        "Launch consumes the session even if exec fails. No key import, overwrite, retry, or receipt recovery.\n", stderr);
    return 2;
}
int main(int argc, char **argv)
{
    struct rlimit limit = {0, 0};
    if (getuid() != geteuid() || getgid() != getegid() || setrlimit(RLIMIT_CORE, &limit)) return 1;
    for (int fd = 0; fd < 3; ++fd) if (fcntl(fd, F_GETFD) < 0) return 1;
    (void)umask(077);
    if (argc < 2) return usage();
    bool create = !strcmp(argv[1], "create"), show = !strcmp(argv[1], "show"), context = !strcmp(argv[1], "context");
    bool review = !strcmp(argv[1], "review"), sign = !strcmp(argv[1], "sign");
    bool launch = !strcmp(argv[1], "launch");
    if ((create && (argc != 4 || strcmp(argv[3], "--acknowledge-experimental"))) ||
        (context && (argc != 5 || strcmp(argv[4], "--acknowledge-experimental"))) ||
        (show && argc != 3) || (review && argc != 8) || (sign && argc != 9) ||
        (launch && (argc != 7 || strcmp(argv[6], "--acknowledge-experimental"))) ||
        !(create || show || context || review || sign || launch)) return usage();
    bool ok = false; int fd = -1;
    if (create) ok = create_identity(argv[2]);
    else {
        fd = directory(argv[2], false);
        if (fd >= 0) {
            if (show) {
                struct tcs_identity identity;
                ok = identity_read(fd, &identity);
                if (ok) { display("realm", identity.realm); display("public_key", identity.public_key); }
            } else if (context) ok = context_create(fd, argv[3]);
            else if (launch) ok = launch_guest(fd, argv[3], argv[4], argv[5]);
            else ok = review_or_sign(fd, argv[3], argv + 4, sign ? argv[8] : NULL);
            close(fd);
        }
    }
    if (fflush(stdout)) ok = false;
    if (!ok) fputs("Refused: invalid input, unsafe file, mismatched context/approval/key, or I/O failure. Existing/partial files were preserved.\n", stderr);
    return ok ? 0 : 1;
}
