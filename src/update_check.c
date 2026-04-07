#include "update_check.h"
#include "output.h"
#include "util.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* TTL after which the cache is considered stale and we refresh.
 * 24h keeps the GitHub API call rate negligible while still picking
 * up releases the same day they ship for an active user. */
#define UPDATE_CHECK_TTL_SECS (24 * 60 * 60)

/* ── Common: cache path + skip-list ──────────────────────────── */

static char *cache_path(void)
{
    char *dir = todoc_dir_path();
    if (!dir) {
        return NULL;
    }
    size_t need = strlen(dir) + sizeof("/update_check");
    char *path = malloc(need);
    if (!path) {
        free(dir);
        return NULL;
    }
    snprintf(path, need, "%s/update_check", dir);
    free(dir);
    return path;
}

/* Commands where the warning would be noisy or self-referential. */
static int command_is_noisy(command_t cmd)
{
    return cmd == CMD_NONE || cmd == CMD_HELP || cmd == CMD_VERSION || cmd == CMD_UPDATE ||
           cmd == CMD_CHANGELOG || cmd == CMD_MODE;
}

int update_check_disabled(command_t cmd)
{
    if (command_is_noisy(cmd)) {
        return 1;
    }
    /* Agents don't want a free-floating update warning interleaved
     * with their JSON envelope. Always silent in ai mode. */
    if (output_is_ai()) {
        return 1;
    }
    const char *env = getenv("TODOC_NO_UPDATE_CHECK");
    if (env && env[0] != '\0' && strcmp(env, "0") != 0) {
        return 1;
    }
    return 0;
}

/* ── Cache I/O ──────────────────────────────────────────────── */

typedef struct {
    long last_check;
    char latest_version[32];
    int breaking;
    int valid;
} cache_t;

static void cache_load(cache_t *out)
{
    memset(out, 0, sizeof(*out));
    char *path = cache_path();
    if (!path) {
        return;
    }
    FILE *f = fopen(path, "r");
    free(path);
    if (!f) {
        return;
    }
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        char *val = eq + 1;
        /* strip trailing newline */
        size_t len = strlen(val);
        while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r')) {
            val[--len] = '\0';
        }
        if (strcmp(line, "last_check") == 0) {
            out->last_check = strtol(val, NULL, 10);
        } else if (strcmp(line, "latest_version") == 0) {
            strncpy(out->latest_version, val, sizeof(out->latest_version) - 1);
        } else if (strcmp(line, "breaking") == 0) {
            out->breaking = atoi(val) != 0;
        }
    }
    fclose(f);
    if (out->latest_version[0] != '\0') {
        out->valid = 1;
    }
}

static int cache_is_fresh(const cache_t *c)
{
    if (!c->valid) {
        return 0;
    }
    time_t now = time(NULL);
    return (now - (time_t)c->last_check) < UPDATE_CHECK_TTL_SECS;
}

/* ── Semver compare ─────────────────────────────────────────── */

static const char *strip_v(const char *s)
{
    return (s && (s[0] == 'v' || s[0] == 'V')) ? s + 1 : s;
}

static int parse_semver(const char *s, int *maj, int *min, int *pat)
{
    if (!s) {
        return -1;
    }
    s = strip_v(s);
    return sscanf(s, "%d.%d.%d", maj, min, pat) == 3 ? 0 : -1;
}

/* Returns 1 if `cached` > `current`, with the delta classification
 * stored in *out_delta: 0 = none/older, 1 = patch, 2 = minor, 3 = major. */
static int compare_versions(const char *cached, const char *current, int *out_delta)
{
    int cmaj = 0, cmin = 0, cpat = 0;
    int rmaj = 0, rmin = 0, rpat = 0;
    if (parse_semver(current, &cmaj, &cmin, &cpat) != 0) {
        return 0;
    }
    if (parse_semver(cached, &rmaj, &rmin, &rpat) != 0) {
        return 0;
    }
    if (rmaj > cmaj) {
        *out_delta = 3;
        return 1;
    }
    if (rmaj < cmaj) {
        return 0;
    }
    if (rmin > cmin) {
        *out_delta = 2;
        return 1;
    }
    if (rmin < cmin) {
        return 0;
    }
    if (rpat > cpat) {
        *out_delta = 1;
        return 1;
    }
    return 0;
}

/* ── Async refresh (double-fork detach) ──────────────────────── */

/* The shell snippet that does the actual GitHub API call. Quoting is
 * delicate — single quotes around the whole string, doubled inside via
 * sh's ' "..." ' trick. We pass it via execl("/bin/sh", "-c", ...). */
static const char *refresh_script =
    "set -e\n"
    "DIR=\"$HOME/.todoc\"\n"
    "mkdir -p \"$DIR\"\n"
    "TMP=$(mktemp \"$DIR/.update_check.XXXXXX\") || exit 0\n"
    "trap 'rm -f \"$TMP\"' EXIT\n"
    "JSON=$(curl -fsSL --max-time 5 "
    "    https://api.github.com/repos/arthur-lonfils/ToDoC/releases/latest 2>/dev/null) || exit 0\n"
    "TAG=$(printf '%s' \"$JSON\" "
    "    | sed -n 's/.*\"tag_name\":[[:space:]]*\"v\\{0,1\\}\\([^\"]*\\)\".*/\\1/p' "
    "    | head -n1)\n"
    "[ -z \"$TAG\" ] && exit 0\n"
    "BREAKING=0\n"
    "if printf '%s' \"$JSON\" | grep -qE 'BREAKING|Breaking Changes|⚠ Breaking'; then\n"
    "  BREAKING=1\n"
    "fi\n"
    "{\n"
    "  echo \"last_check=$(date +%s)\"\n"
    "  echo \"latest_version=$TAG\"\n"
    "  echo \"breaking=$BREAKING\"\n"
    "} > \"$TMP\"\n"
    "mv \"$TMP\" \"$DIR/update_check\"\n"
    "trap - EXIT\n";

void update_check_refresh_async(command_t cmd)
{
    if (update_check_disabled(cmd)) {
        return;
    }

    cache_t cur = {0};
    cache_load(&cur);
    if (cache_is_fresh(&cur)) {
        return; /* nothing to do */
    }

    /* Reap any prior orphans cheaply. */
    signal(SIGCHLD, SIG_IGN);

    pid_t p1 = fork();
    if (p1 < 0) {
        return; /* fork failed — silent give-up */
    }
    if (p1 > 0) {
        /* Parent: wait for the intermediate child to exit so we don't
         * leak a zombie. The grandchild has been re-parented to init. */
        waitpid(p1, NULL, 0);
        return;
    }

    /* Intermediate child: detach into a new session and fork once more
     * so the worker has no controlling terminal and is parented to init. */
    if (setsid() < 0) {
        _exit(0);
    }
    pid_t p2 = fork();
    if (p2 < 0) {
        _exit(0);
    }
    if (p2 > 0) {
        _exit(0);
    }

    /* Grandchild (the worker). Detach stdio so a slow curl can't keep
     * the user's terminal hooked, and exec /bin/sh on the embedded
     * refresh script. */
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > 2) {
            close(devnull);
        }
    }

    execl("/bin/sh", "sh", "-c", refresh_script, (char *)NULL);
    _exit(0);
}

/* ── Warning display ─────────────────────────────────────────── */

/* Detect terminal colors the cheap way (NO_COLOR + isatty on stderr)
 * so the warning matches the rest of display.c without us pulling in
 * display.h's per-message helpers (which are stdout-oriented). */
static int stderr_color(void)
{
    if (getenv("NO_COLOR")) {
        return 0;
    }
    return isatty(STDERR_FILENO);
}

#define CLR_RESET    "\033[0m"
#define CLR_BOLD_RED "\033[1;31m"
#define CLR_YELLOW   "\033[33m"
#define CLR_DIM      "\033[2m"

void update_check_show_warning(command_t cmd)
{
    if (update_check_disabled(cmd)) {
        return;
    }
    cache_t c = {0};
    cache_load(&c);
    if (!c.valid) {
        return;
    }

    int delta = 0;
    if (!compare_versions(c.latest_version, TODOC_VERSION, &delta)) {
        return; /* current or older */
    }

    /* Make sure the command's own output has hit the terminal before
     * we start writing to stderr, so the warning appears at the end of
     * the visible output rather than getting interleaved above it when
     * stdout is piped or redirected. */
    fflush(stdout);

    int color = stderr_color();
    const char *rst = color ? CLR_RESET : "";
    const char *bold_red = color ? CLR_BOLD_RED : "";
    const char *yellow = color ? CLR_YELLOW : "";
    const char *dim = color ? CLR_DIM : "";

    /* Promote any "newer version" to the top severity if the GitHub
     * release notes contained a breaking-change marker. */
    if (c.breaking || delta == 3) {
        fprintf(stderr,
                "\n%s⚠ Major release v%s available — may contain breaking changes.%s\n"
                "%s  Review 'todoc changelog --since %s' before updating.\n"
                "  Your database is backed up automatically before any migration.%s\n",
                bold_red, c.latest_version, rst, dim, TODOC_VERSION, rst);
    } else if (delta == 2) {
        fprintf(stderr,
                "\n%s⬆ New release v%s available — new features.%s\n"
                "%s  Run 'todoc update' to install. Your database is backed up automatically.%s\n",
                yellow, c.latest_version, rst, dim, rst);
    } else if (delta == 1) {
        fprintf(stderr, "\n%s· Patch v%s available. Run 'todoc update'.%s\n", dim, c.latest_version,
                rst);
    }
}
