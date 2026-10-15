/* mtime OUT cmd args...
 * Fork/exec cmd, wait4 it, and write one line to OUT:
 *   wall_s user_s sys_s maxrss_kb tree_peak_kb exit_code
 * maxrss_kb is wait4's ru_maxrss (max over the child and the descendants it
 * reaped, not their sum). tree_peak_kb is the peak of the summed RSS of the
 * child and all its live descendants, sampled every 50 ms from /proc (so a
 * spawned helper process counts on top of its parent).
 * The launcher itself is ~1 MB, which is the floor ru_maxrss inherits. */
#define _GNU_SOURCE
#include <dirent.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

#define MAXP 65536
static int ppid_of[MAXP];
static long rss_of[MAXP];

static long tree_rss_kb(pid_t root) {
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *e;
    static int pids[MAXP];
    int n = 0;
    long page_kb = sysconf(_SC_PAGESIZE) / 1024;
    while ((e = readdir(d)) && n < MAXP) {
        int pid = atoi(e->d_name);
        if (pid <= 0 || pid >= MAXP) continue;
        char path[64], buf[1024];
        snprintf(path, sizeof path, "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        size_t k = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[k] = 0;
        char *p = strrchr(buf, ')');
        if (!p) continue;
        /* after ") ": state ppid ... field 24 is rss (pages) */
        char state;
        int ppid;
        long fields[22];
        if (sscanf(p + 2, "%c %d", &state, &ppid) != 2) continue;
        /* skip to rss: fields after comm: 3 state,4 ppid,...,24 rss */
        char *q = p + 2;
        int field = 3;
        long rss = 0;
        while (*q && field < 24) {
            if (*q == ' ') field++;
            q++;
        }
        rss = atol(q);
        (void)fields;
        ppid_of[pid] = ppid;
        rss_of[pid] = rss * page_kb;
        pids[n++] = pid;
    }
    closedir(d);
    long total = 0;
    for (int i = 0; i < n; i++) {
        int p = pids[i], hops = 0;
        while (p > 1 && p != root && hops < 64) { p = ppid_of[p]; hops++; }
        if (p == root) total += rss_of[pids[i]];
    }
    for (int i = 0; i < n; i++) { ppid_of[pids[i]] = 0; rss_of[pids[i]] = 0; }
    return total;
}

static volatile int done = 0;
static volatile long peak = 0;
static pid_t child;

static void *sampler(void *arg) {
    (void)arg;
    while (!done) {
        long t = tree_rss_kb(child);
        if (t > peak) peak = t;
        usleep(50000);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: mtime OUT cmd args...\n"); return 2; }
    double t0 = now();
    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[2], argv + 2);
        perror("execvp");
        _exit(127);
    }
    child = pid;
    pthread_t th;
    pthread_create(&th, NULL, sampler, NULL);
    int status = 0;
    struct rusage ru;
    while (wait4(pid, &status, 0, &ru) < 0) {
        if (errno != EINTR) { perror("wait4"); return 2; }
    }
    double wall = now() - t0;
    done = 1;
    pthread_join(th, NULL);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    FILE *o = fopen(argv[1], "w");
    if (o) {
        fprintf(o, "%.3f %.3f %.3f %ld %ld %d\n", wall, ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6,
                ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6, ru.ru_maxrss, peak, code);
        fclose(o);
    }
    return code;
}
