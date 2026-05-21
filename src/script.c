#define safe_strcpy(dst, src) do { snprintf((dst), sizeof(dst), "%s", (src)); } while(0)

#include "sell-shell.h"

static int read_line(FILE *fp, char *line, size_t size) {
    if (!fgets(line, size, fp)) return 0;
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
    return 1;
}

static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

static int eval_cond(const char *cond) {
    pipeline_t p = parse_line((char *)cond);
    int result = execute_pipeline(&p);
    free_pipeline(&p);
    return result == 0;
}

static int exec_cmd_line(const char *line) {
    pipeline_t p = parse_line((char *)line);
    int r = execute_pipeline(&p);
    free_pipeline(&p);
    return r;
}

static int process_line(FILE *fp, const char *line);

static int if_block(FILE *fp, const char *line) {
    const char *p = line + 2;
    skip_ws(&p);

    char cond[MAX_LINE];
    int ci = 0;
    int done = 0;

    while (*p && !done) {
        if (strncmp(p, "then", 4) == 0 && (p[4] == ' ' || p[4] == '\t' || p[4] == ';' || p[4] == '\0')) {
            p += 4;
            done = 1;
        } else if (*p == ';') {
            p++;
            skip_ws(&p);
        } else {
            if (ci < (int)sizeof(cond) - 1) cond[ci++] = *p;
            p++;
        }
    }
    cond[ci] = '\0';

    int cond_true = eval_cond(cond);
    int handling_then = cond_true;
    int handling_else = 0;
    char buf[MAX_LINE];
    int depth = 0;
    int finished = 0;

    /* Read until fi, executing appropriate blocks */
    while (!finished && read_line(fp, buf, sizeof(buf))) {
        const char *bp = buf;
        skip_ws(&bp);

        if (strncmp(bp, "fi", 2) == 0 && (bp[2] == '\0' || bp[2] == ';' || bp[2] == ' ')) {
            if (depth == 0) finished = 1;
            else depth--;
            continue;
        }

        if (strncmp(bp, "else", 4) == 0 && (bp[4] == '\0' || bp[4] == ';' || bp[4] == ' ') && depth == 0) {
            handling_then = 0;
            handling_else = 1;
            continue;
        }

        if (strncmp(bp, "elif", 4) == 0 && (bp[4] == ' ' || bp[4] == '\t') && depth == 0) {
            if (handling_then || handling_else) {
                handling_then = 0;
                handling_else = 0;
            }
            if (!cond_true && !handling_else) {
                if_block(fp, bp);
            }
            continue;
        }

        if (strncmp(bp, "if", 2) == 0 && (bp[2] == ' ' || bp[2] == '\t')) depth++;
        else if (strncmp(bp, "while", 5) == 0 && (bp[5] == ' ' || bp[5] == '\t')) depth++;
        else if (strncmp(bp, "for", 3) == 0 && (bp[3] == ' ' || bp[3] == '\t')) depth++;

        if (handling_then || handling_else) {
            process_line(fp, bp);
        }
    }
    return 0;
}

static int while_block(FILE *fp, const char *line) {
    const char *p = line + 5;
    skip_ws(&p);

    char cond[MAX_LINE];
    int ci = 0;

    while (*p) {
        if (strncmp(p, "do", 2) == 0 && (p[2] == ' ' || p[2] == '\t' || p[2] == ';' || p[2] == '\0')) {
            p += 2;
            break;
        }
        if (*p == ';') { p++; break; }
        if (ci < (int)sizeof(cond) - 1) cond[ci++] = *p;
        p++;
    }
    cond[ci] = '\0';

    /* After ; look for do */
    skip_ws(&p);
    if (strncmp(p, "do", 2) == 0) { /* good */ }

    /* Read body */
    char **body = NULL;
    int num_body = 0, body_cap = 0;
    char buf[MAX_LINE];
    int depth = 0;

    while (read_line(fp, buf, sizeof(buf))) {
        const char *bp = buf;
        skip_ws(&bp);

        if (strncmp(bp, "done", 4) == 0 && (bp[4] == '\0' || bp[4] == ';' || bp[4] == ' ')) {
            if (depth == 0) break;
            depth--;
        }
        if (strncmp(bp, "if", 2) == 0 && (bp[2] == ' ' || bp[2] == '\t')) depth++;
        else if (strncmp(bp, "while", 5) == 0 && (bp[5] == ' ' || bp[5] == '\t')) depth++;
        else if (strncmp(bp, "for", 3) == 0 && (bp[3] == ' ' || bp[3] == '\t')) depth++;
        else if (strncmp(bp, "fi", 2) == 0 && depth > 0 && (bp[2] == '\0' || bp[2] == ';')) depth--;
        else if (strncmp(bp, "done", 4) == 0 && depth > 0) depth--;

        if (num_body >= body_cap) {
            body_cap = body_cap ? body_cap * 2 : 64;
            body = realloc(body, body_cap * sizeof(char *));
        }
        body[num_body++] = strdup(buf);
    }

    while (eval_cond(cond)) {
        for (int i = 0; i < num_body; i++) {
            process_line(fp, body[i]);
        }
    }

    for (int i = 0; i < num_body; i++) free(body[i]);
    free(body);
    return 0;
}

static int for_block(FILE *fp, const char *line) {
    const char *p = line + 3;
    skip_ws(&p);

    char var[64];
    int vi = 0;
    while (*p && !isspace(*p) && vi < 62) var[vi++] = *p++;
    var[vi] = '\0';
    skip_ws(&p);

    char words[MAX_ARGS][MAX_LINE];
    int nw = 0;

    if (strncmp(p, "in", 2) == 0) {
        p += 2;
        skip_ws(&p);
        char w[MAX_LINE];
        int wi = 0;
        while (*p && nw < MAX_ARGS - 1) {
            if (isspace(*p) || *p == ';') {
                if (wi > 0) { w[wi] = '\0'; safe_strcpy(words[nw++], w); wi = 0; }
                if (*p == ';') { p++; break; }
                skip_ws(&p);
                continue;
            }
            if (wi < MAX_LINE - 1) w[wi++] = *p;
            p++;
        }
        if (wi > 0) { w[wi] = '\0'; safe_strcpy(words[nw++], w); }
    }

    skip_ws(&p);
    if (strncmp(p, "do", 2) == 0) { /* good */ }

    char **body = NULL;
    int num_body = 0, body_cap = 0;
    char buf[MAX_LINE];
    int depth = 0;

    while (read_line(fp, buf, sizeof(buf))) {
        const char *bp = buf;
        skip_ws(&bp);

        if (strncmp(bp, "done", 4) == 0 && (bp[4] == '\0' || bp[4] == ';' || bp[4] == ' ')) {
            if (depth == 0) break;
            depth--;
        }
        if (strncmp(bp, "if", 2) == 0 && (bp[2] == ' ' || bp[2] == '\t')) depth++;
        else if (strncmp(bp, "while", 5) == 0 && (bp[5] == ' ' || bp[5] == '\t')) depth++;
        else if (strncmp(bp, "for", 3) == 0 && (bp[3] == ' ' || bp[3] == '\t')) depth++;
        else if (strncmp(bp, "fi", 2) == 0 && depth > 0 && (bp[2] == '\0' || bp[2] == ';')) depth--;
        else if (strncmp(bp, "done", 4) == 0 && depth > 0) depth--;

        if (num_body >= body_cap) {
            body_cap = body_cap ? body_cap * 2 : 64;
            body = realloc(body, body_cap * sizeof(char *));
        }
        body[num_body++] = strdup(buf);
    }

    for (int w = 0; w < nw; w++) {
        setenv(var, words[w], 1);
        for (int i = 0; i < num_body; i++) {
            process_line(fp, body[i]);
        }
    }
    for (int i = 0; i < num_body; i++) free(body[i]);
    free(body);
    return 0;
}

static int process_line(FILE *fp, const char *line) {
    const char *p = line;
    skip_ws(&p);
    if (!*p || *p == '#') return 0;

    if (strncmp(p, "if", 2) == 0 && (p[2] == ' ' || p[2] == '\t')) return if_block(fp, p);
    if (strncmp(p, "while", 5) == 0 && (p[5] == ' ' || p[5] == '\t')) return while_block(fp, p);
    if (strncmp(p, "for", 3) == 0 && (p[3] == ' ' || p[3] == '\t')) return for_block(fp, p);

    return exec_cmd_line(p);
}

int execute_script(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "sell-shell: %s: %s\n", filename, strerror(errno));
        return 1;
    }

    char line[MAX_LINE];
    while (read_line(fp, line, sizeof(line))) {
        process_line(fp, line);
    }
    fclose(fp);
    return last_exit_code;
}

int execute_script_line(const char *line) {
    return exec_cmd_line(line);
}
