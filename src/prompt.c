#include "sell-shell.h"

char *generate_prompt(void) {
    static char prompt[MAX_LINE];
    char *ps1 = getenv("PS1");
    if (!ps1) ps1 = "\\[\\e[32m\\]\\u@\\h\\[\\e[0m\\]:\\[\\e[34m\\]\\w\\[\\e[0m\\]\\$ ";

    char result[MAX_LINE * 2] = "";
    int ri = 0;
    int last_was_escape = 0;

    for (int i = 0; ps1[i] && ri < (int)sizeof(result) - 20; i++) {
        if (ps1[i] == '\\' && !last_was_escape) {
            last_was_escape = 1;
            continue;
        }

        if (last_was_escape) {
            last_was_escape = 0;
            switch (ps1[i]) {
                case 'u': {
                    struct passwd *pw = getpwuid(getuid());
                    ri += snprintf(result + ri, sizeof(result) - ri, "%s", pw ? pw->pw_name : "unknown");
                    break;
                }
                case 'h': {
                    char hostname[256];
                    gethostname(hostname, sizeof(hostname));
                    char *dot = strchr(hostname, '.');
                    if (dot) *dot = '\0';
                    ri += snprintf(result + ri, sizeof(result) - ri, "%s", hostname);
                    break;
                }
                case 'H': {
                    char hostname[256];
                    gethostname(hostname, sizeof(hostname));
                    ri += snprintf(result + ri, sizeof(result) - ri, "%s", hostname);
                    break;
                }
                case 'w': {
                    char cwd[MAX_LINE];
                    if (getcwd(cwd, sizeof(cwd))) {
                        char *home = getenv("HOME");
                        if (home && strncmp(cwd, home, strlen(home)) == 0) {
                            ri += snprintf(result + ri, sizeof(result) - ri, "~%s", cwd + strlen(home));
                        } else {
                            ri += snprintf(result + ri, sizeof(result) - ri, "%s", cwd);
                        }
                    }
                    break;
                }
                case 'W': {
                    char cwd[MAX_LINE];
                    if (getcwd(cwd, sizeof(cwd))) {
                        char *base = strrchr(cwd, '/');
                        ri += snprintf(result + ri, sizeof(result) - ri, "%s", base ? base + 1 : cwd);
                    }
                    break;
                }
                case 'd': {
                    time_t t = time(NULL);
                    struct tm *tm = localtime(&t);
                    char date[64];
                    strftime(date, sizeof(date), "%a %b %d", tm);
                    ri += snprintf(result + ri, sizeof(result) - ri, "%s", date);
                    break;
                }
                case 't': {
                    time_t t = time(NULL);
                    struct tm *tm = localtime(&t);
                    char timebuf[64];
                    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm);
                    ri += snprintf(result + ri, sizeof(result) - ri, "%s", timebuf);
                    break;
                }
                case 'T': {
                    time_t t = time(NULL);
                    struct tm *tm = localtime(&t);
                    char timebuf[64];
                    strftime(timebuf, sizeof(timebuf), "%I:%M:%S", tm);
                    ri += snprintf(result + ri, sizeof(result) - ri, "%s", timebuf);
                    break;
                }
                case '@': {
                    time_t t = time(NULL);
                    struct tm *tm = localtime(&t);
                    char timebuf[64];
                    strftime(timebuf, sizeof(timebuf), "%I:%M %p", tm);
                    ri += snprintf(result + ri, sizeof(result) - ri, "%s", timebuf);
                    break;
                }
                case 'n':
                    result[ri++] = '\n';
                    break;
                case 's':
                    ri += snprintf(result + ri, sizeof(result) - ri, "sell-shell");
                    break;
                case 'v':
                    ri += snprintf(result + ri, sizeof(result) - ri, "2.0");
                    break;
                case '$':
                    result[ri++] = (getuid() == 0) ? '#' : '$';
                    break;
                case '\\':
                    result[ri++] = '\\';
                    break;
                case '[':
                    result[ri++] = '\001';
                    break;
                case ']':
                    result[ri++] = '\002';
                    break;
                case 'e':
                    result[ri++] = '\033';
                    break;
                default:
                    result[ri++] = ps1[i];
                    break;
            }
        } else {
            result[ri++] = ps1[i];
        }
    }
    result[ri] = '\0';

    /* Append git status if in a git repo */
    if (access(".git/HEAD", F_OK) == 0 || access("../.git/HEAD", F_OK) == 0) {
        /* Check parent dirs for .git */
        char cwd[MAX_LINE];
        char git_dir[MAX_LINE + 16];
        int found = 0;
        if (getcwd(cwd, sizeof(cwd))) {
            char *p = cwd;
            while (1) {
                snprintf(git_dir, sizeof(git_dir), "%s/.git/HEAD", p);
                if (access(git_dir, F_OK) == 0) { found = 1; break; }
                char *slash = strrchr(p, '/');
                if (!slash || slash == p) {
                    if (strlen(p) > 1 && access("/.git/HEAD", F_OK) == 0) { found = 1; break; }
                    break;
                }
                *slash = '\0';
            }
        }

        if (found) {
            FILE *fp = fopen(git_dir, "r");
            if (fp) {
                char line[256];
                if (fgets(line, sizeof(line), fp)) {
                    char *ref = strstr(line, "refs/heads/");
                    if (ref) {
                        ref += 11;
                        char *nl = strchr(ref, '\n');
                        if (nl) *nl = '\0';

                        /* Check for dirty state */
                        int dirty = 0;
                        FILE *status_fp = popen("git status --porcelain 2>/dev/null", "r");
                        if (status_fp) {
                            char buf[256];
                            if (fgets(buf, sizeof(buf), status_fp)) dirty = 1;
                            pclose(status_fp);
                        }

                        ri += snprintf(result + ri, sizeof(result) - ri,
                            " \001\033[33m\002(%s%s)\001\033[0m\002",
                            ref, dirty ? "+" : "");
                    }
                }
                fclose(fp);
            }
        }
    }

    snprintf(prompt, sizeof(prompt), "%s", result);
    return prompt;
}
