/* gcc-nosan -- Windows host-test compiler shim for the M1 firmware repo.
 *
 * Why this exists (documentation/merge1-baseline-evidence.md section 5):
 * the only host C compiler on the verified Windows host (WinLibs MinGW-w64
 * gcc 15.2.0) ships WITHOUT the ASan/UBSan runtimes, but the upstream host
 * test harnesses hardcode `-fsanitize=address,undefined` in their compile
 * commands (`ld: cannot find -lasan`, `-lubsan`). Additionally, gcc 15
 * promotes three warning classes to errors on the harnesses' GENERATED
 * stub code that were plain warnings on the project's reference gcc 14:
 *   -Werror=sign-compare, -Werror=misleading-indentation and
 *   -Werror=pointer-to-int-cast (the last is a 64-bit-host vs 32-bit-target
 *   delta: the tests cast device register addresses like (uint32_t)&REG).
 *
 * What it does:
 *   1. strips every -fsanitize= option whose sanitizer set is only
 *      {address, undefined} (single or combined form);
 *   2. appends -Wno-error=<class> for each of the three demoted classes
 *      whenever the command line carries -Werror in any form (bare or
 *      classed) -- the harnesses use both spellings.
 *   3. execs the real compiler (env GCC_NOSAN_CC, default "gcc") and
 *      propagates its exit status.
 *
 * Runs are therefore UNINSTRUMENTED: all test assertions execute, but
 * ASan/UBSan memory-safety coverage is absent on this host (recorded in
 * the baseline evidence; not silently lost).
 *
 * Build (once, with the host gcc):
 *   gcc -O2 -o gcc-nosan.exe tools/gcc-nosan/gcc-nosan.c
 *
 * Use:
 *   CC=/path/to/gcc-nosan.exe python m1_csrc/test/m1_pass5_reliability_test.py
 * For the three harnesses that hardcode `cc` (no env override exists),
 * copy gcc-nosan.exe to cc.exe in a directory prepended to PATH.
 */

#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sanitizers this shim is allowed to strip: everything else (e.g. fuzzer)
 * is passed through untouched so the shim can never silently weaken a
 * sanitizer the host actually supports. */
static int sanitize_is_nosan_only(const char *arg)
{
    const char *s = arg;
    int saw_address = 0, saw_undefined = 0, saw_other = 0;

    if (strncmp(s, "-fsanitize=", 11) != 0)
        return 0;
    s += 11;

    while (*s) {
        size_t len;
        const char *comma = strchr(s, ',');
        len = comma ? (size_t)(comma - s) : strlen(s);
        if (len == 7 && strncmp(s, "address", 7) == 0)
            saw_address = 1;
        else if (len == 9 && strncmp(s, "undefined", 9) == 0)
            saw_undefined = 1;
        else
            saw_other = 1;
        if (!comma)
            break;
        s = comma + 1;
    }
    return (saw_address || saw_undefined) && !saw_other;
}

static int startswith(const char *s, const char *pfx)
{
    return strncmp(s, pfx, strlen(pfx)) == 0;
}

int main(int argc, char **argv)
{
    const char *real_cc = getenv("GCC_NOSAN_CC");
    char **nargv;
    int i, n = 0, saw_werror = 0;

    if (real_cc == NULL || *real_cc == '\0')
        real_cc = "gcc";

    /* argv[0] slot + up to (argc-1) passthrough args + 3 demotions + NULL */
    nargv = malloc(sizeof(char *) * (argc + 4));
    if (nargv == NULL) {
        fputs("gcc-nosan: out of memory\n", stderr);
        return 127;
    }
    nargv[n++] = (char *)real_cc;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (sanitize_is_nosan_only(a))
            continue; /* stripped */
        nargv[n++] = argv[i];
        /* Any -Werror form (bare or classed) promotes the three gcc-15
         * classes that fire on test-GENERATED stub code (baseline
         * evidence section 5); remember to append the demotions after
         * the full command line so they win. */
        if (strcmp(a, "-Werror") == 0 || startswith(a, "-Werror="))
            saw_werror = 1;
    }
    if (saw_werror) {
        static const char *const classes[] = {
            "sign-compare", "misleading-indentation", "pointer-to-int-cast"};
        for (i = 0; i < 3; i++) {
            char *buf = malloc(strlen(classes[i]) + 12);
            if (buf == NULL) {
                fputs("gcc-nosan: out of memory\n", stderr);
                return 127;
            }
            strcpy(buf, "-Wno-error=");
            strcat(buf, classes[i]);
            nargv[n++] = buf;
        }
    }
    nargv[n] = NULL;

    fflush(NULL);
    {
        int rc = (int)_spawnvp(_P_WAIT, real_cc, (const char *const *)nargv);
        if (rc == -1) {
            fprintf(stderr,
                    "gcc-nosan: failed to execute '%s' (set GCC_NOSAN_CC to "
                    "the real host compiler)\n",
                    real_cc);
            return 127;
        }
        return rc;
    }
    /* not reached */
    return 0;
}
