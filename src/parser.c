#include "sell-shell.h"

static char expansion_buf[MAX_LINE];
static int exp_idx;

static void exp_clear(void) {
    exp_idx = 0;
    expansion_buf[0] = '\0';
}

static void exp_append(char c) {
    if (exp_idx < MAX_LINE - 1) {
        expansion_buf[exp_idx++] = c;
        expansion_buf[exp_idx] = '\0';
    }
}

static void exp_appends(const char *s) {
    while (s && *s) exp_append(*s++);
}

static char *expand_single_var(const char **p) {
    (*p)++;
    if (**p == '{') {
        (*p)++;
        char name[256];
        int ni = 0;
        while (**p && **p != '}' && ni < 254) name[ni++] = *(*p)++;
        if (**p == '}') (*p)++;
        name[ni] = '\0';
        if (strcmp(name, "?") == 0) {
            char ec[16];
            snprintf(ec, sizeof(ec), "%d", last_exit_code);
            exp_appends(ec);
        } else {
            char *val = getenv(name);
            if (val) exp_appends(val);
        }
    } else if (**p == '?') {
        (*p)++;
        char ec[16];
        snprintf(ec, sizeof(ec), "%d", last_exit_code);
        exp_appends(ec);
    } else if (**p == '$') {
        (*p)++;
        exp_appends("$");
    } else {
        char name[256];
        int ni = 0;
        while (**p && (isalnum(**p) || **p == '_') && ni < 254) name[ni++] = *(*p)++;
        name[ni] = '\0';
        if (ni > 0) {
            char *val = getenv(name);
            if (val) exp_appends(val);
        } else {
            exp_append('$');
        }
    }
    return expansion_buf;
}

static int is_dquote_special(char c) {
    return c == '$' || c == '`' || c == '"' || c == '\\' || c == '\n';
}

char *expand_variables_internal(const char *word, int in_double_quote) {
    exp_clear();
    while (*word) {
        if (*word == '\\') {
            word++;
            if (*word) {
                if (in_double_quote && !is_dquote_special(*word)) {
                    exp_append('\\');
                    continue;
                }
                exp_append(*word); word++;
            }
        } else if (*word == '\'' && !in_double_quote) {
            word++;
            while (*word && *word != '\'') exp_append(*word++);
            if (*word == '\'') word++;
        } else if (*word == '"' && !in_double_quote) {
            word++;
            while (*word && *word != '"') {
                if (*word == '\\') {
                    word++;
                    if (*word) {
                        if (!is_dquote_special(*word)) {
                            exp_append('\\');
                            continue;
                        }
                        exp_append(*word); word++;
                    }
                } else if (*word == '$') {
                    expand_single_var(&word);
                } else {
                    exp_append(*word); word++;
                }
            }
            if (*word == '"') word++;
        } else if (*word == '$') {
            expand_single_var(&word);
        } else {
            exp_append(*word); word++;
        }
    }
    return strdup(expansion_buf);
}

char *expand_variables(const char *word) {
    return expand_variables_internal(word, 0);
}

static int is_special_char(char c) {
    return c == '|' || c == '<' || c == '>' || c == '&';
}

static int get_token(const char **p, char *buf, size_t bufsize) {
    while (**p && isspace(**p)) (*p)++;
    if (!**p) return 0;

    size_t bi = 0;
    buf[0] = '\0';

    if (**p == '|') { buf[bi++] = *(*p)++; buf[bi] = '\0'; return 1; }
    if (**p == '<') { buf[bi++] = *(*p)++; buf[bi] = '\0'; return 1; }
    if (**p == '>') {
        buf[bi++] = *(*p)++;
        if (**p == '>') { buf[bi++] = *(*p)++; }
        buf[bi] = '\0';
        return 1;
    }
    if (**p == '&') {
        buf[bi++] = *(*p)++;
        if (**p == '>') {
            buf[bi++] = *(*p)++;
            if (**p == '>') { buf[bi++] = *(*p)++; }
        }
        buf[bi] = '\0';
        return 1;
    }
    if (**p == '2' && *(*p+1) == '>') {
        buf[bi++] = *(*p)++;
        buf[bi++] = *(*p)++;
        if (**p == '>') { buf[bi++] = *(*p)++; }
        buf[bi] = '\0';
        return 1;
    }

    while (**p && !isspace(**p) && !is_special_char(**p)) {
        if (bi >= bufsize - 6) break;

        if (**p == '\'') {
            buf[bi++] = *(*p)++;
            while (**p && **p != '\'') {
                buf[bi++] = *(*p)++;
                if (bi >= bufsize - 2) break;
            }
            if (**p == '\'') { buf[bi++] = *(*p)++; }
        } else if (**p == '"') {
            buf[bi++] = *(*p)++;
            while (**p && **p != '"') {
                if (**p == '\\') { buf[bi++] = *(*p)++; }
                buf[bi++] = *(*p)++;
                if (bi >= bufsize - 2) break;
            }
            if (**p == '"') { buf[bi++] = *(*p)++; }
        } else if (**p == '\\') {
            buf[bi++] = *(*p)++;
            if (**p) { buf[bi++] = *(*p)++; }
        } else {
            buf[bi++] = *(*p)++;
        }
    }
    buf[bi] = '\0';
    return 1;
}

pipeline_t parse_line(char *line) {
    pipeline_t p = {0};
    p.background = 0;
    p.cmd_str = strdup(line);
    p.num_commands = 1;

    for (const char *s = line; *s; s++) {
        if (*s == '|') p.num_commands++;
    }

    p.commands = calloc(p.num_commands, sizeof(command_t));

    char **raw_tokens = calloc(MAX_ARGS, sizeof(char *));
    char *exp_tokens[MAX_ARGS];
    int num_raw = 0;
    const char *cursor = line;
    char token_buf[MAX_LINE];

    while (num_raw < MAX_ARGS - 1 && get_token(&cursor, token_buf, sizeof(token_buf))) {
        raw_tokens[num_raw] = strdup(token_buf);
        num_raw++;
    }

    int num_exp = 0;
    int cmd_start = 1;

    for (int i = 0; i < num_raw && num_exp < MAX_ARGS - 1; i++) {
        char *tok = raw_tokens[i];

        if (strcmp(tok, "|") == 0 || strcmp(tok, "<") == 0 || strcmp(tok, ">") == 0 ||
            strcmp(tok, ">>") == 0 || strcmp(tok, "2>") == 0 || strcmp(tok, "2>>") == 0 ||
            strcmp(tok, "&>") == 0 || strcmp(tok, "&>>") == 0 || strcmp(tok, "&") == 0) {
            exp_tokens[num_exp++] = strdup(tok);
            if (strcmp(tok, "|") == 0) cmd_start = 1;
            continue;
        }

        int expanded = 0;
        if (cmd_start) {
            alias_t *a = aliases;
            while (a) {
                if (strcmp(tok, a->name) == 0) {
                    char alias_copy[MAX_LINE];
                    snprintf(alias_copy, sizeof(alias_copy), "%s", a->value);
                    const char *ac = alias_copy;
                    char atok[MAX_LINE];
                    while (get_token(&ac, atok, sizeof(atok)) && num_exp < MAX_ARGS - 1) {
                        exp_tokens[num_exp++] = expand_variables(atok);
                    }
                    expanded = 1;
                    break;
                }
                a = a->next;
            }
        }
        if (!expanded) {
            exp_tokens[num_exp++] = expand_variables(tok);
        }
        cmd_start = 0;

        if (strcmp(tok, "\"") == 0 || strcmp(tok, "'") == 0) {
            /* bare quote token - don't set cmd_start */
        }
    }
    exp_tokens[num_exp] = NULL;

    int ti = 0;
    int cmd_idx = 0;
    while (ti < num_exp && cmd_idx < p.num_commands) {
        command_t *cmd = &p.commands[cmd_idx];
        cmd->args = calloc(MAX_ARGS, sizeof(char *));
        cmd->num_redirects = 0;
        cmd->redirects = calloc(MAX_ARGS / 2, sizeof(redir_t));
        int ai = 0;

        while (ti < num_exp) {
            char *tok = exp_tokens[ti];

            if (strcmp(tok, "|") == 0) {
                ti++;
                break;
            }

            if (strcmp(tok, "&") == 0) {
                p.background = 1;
                ti++;
                continue;
            }

            int redir_type = 0;
            int redirect_fd = -1;

            if (strcmp(tok, "<") == 0)          { redir_type = REDIR_IN; redirect_fd = STDIN_FILENO; }
            else if (strcmp(tok, ">") == 0)     { redir_type = REDIR_OUT; redirect_fd = STDOUT_FILENO; }
            else if (strcmp(tok, ">>") == 0)    { redir_type = REDIR_APPEND; redirect_fd = STDOUT_FILENO; }
            else if (strcmp(tok, "2>") == 0)    { redir_type = REDIR_ERR; redirect_fd = STDERR_FILENO; }
            else if (strcmp(tok, "2>>") == 0)   { redir_type = REDIR_ERR_APPEND; redirect_fd = STDERR_FILENO; }
            else if (strcmp(tok, "&>") == 0)    { redir_type = REDIR_ALL; redirect_fd = STDOUT_FILENO; }
            else if (strcmp(tok, "&>>") == 0)   { redir_type = REDIR_ALL; redirect_fd = STDOUT_FILENO; }

            if (redir_type != 0) {
                ti++;
                if (ti >= num_exp) break;
                redir_t *r = &cmd->redirects[cmd->num_redirects++];
                r->type = redir_type;
                r->file = strdup(exp_tokens[ti]);
                r->fd = redirect_fd;
                ti++;
                continue;
            }

            cmd->args[ai++] = strdup(tok);
            ti++;
        }
        cmd->args[ai] = NULL;
        cmd_idx++;
    }

    for (int i = 0; i < num_exp; i++) free(exp_tokens[i]);
    for (int i = 0; i < num_raw; i++) free(raw_tokens[i]);
    free(raw_tokens);
    return p;
}

void free_pipeline(pipeline_t *p) {
    if (!p) return;
    for (int i = 0; i < p->num_commands; i++) {
        command_t *cmd = &p->commands[i];
        if (cmd->args) {
            for (int j = 0; cmd->args[j]; j++) free(cmd->args[j]);
            free(cmd->args);
        }
        for (int j = 0; j < cmd->num_redirects; j++) {
            free(cmd->redirects[j].file);
        }
        free(cmd->redirects);
    }
    free(p->commands);
    free(p->cmd_str);
}
