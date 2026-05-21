#include "sell-shell.h"

int last_exit_code = 0;

static void pipeline_deep_copy(pipeline_t *dst, const pipeline_t *src) {
    dst->num_commands = src->num_commands;
    dst->background = src->background;
    dst->cmd_str = src->cmd_str ? strdup(src->cmd_str) : NULL;
    dst->commands = calloc(src->num_commands, sizeof(command_t));
    for (int i = 0; i < src->num_commands; i++) {
        command_t *sd = &dst->commands[i];
        command_t *ss = &src->commands[i];
        sd->num_redirects = ss->num_redirects;
        int ac = 0;
        while (ss->args && ss->args[ac]) ac++;
        sd->args = calloc(ac + 1, sizeof(char *));
        for (int j = 0; j < ac; j++) sd->args[j] = strdup(ss->args[j]);
        sd->args[ac] = NULL;
        sd->redirects = calloc(ss->num_redirects > 0 ? ss->num_redirects : 1, sizeof(redir_t));
        for (int j = 0; j < ss->num_redirects; j++) {
            sd->redirects[j].type = ss->redirects[j].type;
            sd->redirects[j].fd = ss->redirects[j].fd;
            sd->redirects[j].file = ss->redirects[j].file ? strdup(ss->redirects[j].file) : NULL;
        }
    }
}

static int apply_redirects(command_t *cmd) {
    for (int i = 0; i < cmd->num_redirects; i++) {
        redir_t *r = &cmd->redirects[i];
        int fd = -1;

        switch (r->type) {
            case REDIR_IN:
                fd = open(r->file, O_RDONLY);
                if (fd < 0) { perror(r->file); return -1; }
                if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
                close(fd);
                break;
            case REDIR_OUT:
                fd = open(r->file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { perror(r->file); return -1; }
                if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
                close(fd);
                break;
            case REDIR_APPEND:
                fd = open(r->file, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd < 0) { perror(r->file); return -1; }
                if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
                close(fd);
                break;
            case REDIR_ERR:
                fd = open(r->file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { perror(r->file); return -1; }
                if (dup2(fd, STDERR_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
                close(fd);
                break;
            case REDIR_ERR_APPEND:
                fd = open(r->file, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd < 0) { perror(r->file); return -1; }
                if (dup2(fd, STDERR_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
                close(fd);
                break;
            case REDIR_ALL:
                fd = open(r->file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { perror(r->file); return -1; }
                if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
                if (dup2(fd, STDERR_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
                close(fd);
                break;
        }
    }
    return 0;
}

static void child_exec(command_t *cmd, int input_fd, int output_fd) {
    if (input_fd != STDIN_FILENO) {
        dup2(input_fd, STDIN_FILENO);
        close(input_fd);
    }
    if (output_fd != STDOUT_FILENO) {
        dup2(output_fd, STDOUT_FILENO);
        close(output_fd);
    }

    if (apply_redirects(cmd) != 0) exit(EXIT_FAILURE);

    if (is_builtin(cmd->args[0])) {
        exit(execute_builtin(cmd->args));
    }

    execvp(cmd->args[0], cmd->args);
    fprintf(stderr, "sell-shell: %s: %s\n", cmd->args[0], strerror(errno));
    exit(EXIT_FAILURE);
}

int execute_pipeline(pipeline_t *p) {
    int num_cmds = p->num_commands;
    int prev_fd = STDIN_FILENO;
    pid_t pids[MAX_JOBS];
    int num_pids = 0;

    if (num_cmds == 1 && p->commands[0].args && p->commands[0].args[0]) {
        char *cmd_name = p->commands[0].args[0];

        if (is_builtin(cmd_name)) {
            if (p->commands[0].num_redirects > 0) {
                pid_t pid = fork();
                if (pid == 0) {
                    setpgid(0, 0);
                    if (apply_redirects(&p->commands[0]) != 0) exit(EXIT_FAILURE);
                    exit(execute_builtin(p->commands[0].args));
                } else if (pid > 0) {
                    if (!p->background) {
                        int status;
                        waitpid(pid, &status, 0);
                        last_exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
                    }
                }
                return 0;
            }
            if (!p->background) {
                last_exit_code = execute_builtin(p->commands[0].args);
                return 0;
            }
        }
    }

    fflush(NULL);

    for (int i = 0; i < num_cmds; i++) {
        int pipefd[2] = {STDIN_FILENO, STDOUT_FILENO};

        if (i < num_cmds - 1) {
            if (pipe(pipefd) < 0) {
                perror("pipe");
                return 1;
            }
        }

        pid_t pid = fork();
        if (pid == 0) {
            if (shell_is_interactive) setpgid(0, p->background ? 0 : shell_pgid);

            if (i == 0) {
                child_exec(&p->commands[i], STDIN_FILENO, pipefd[1]);
            } else if (i == num_cmds - 1) {
                child_exec(&p->commands[i], prev_fd, STDOUT_FILENO);
            } else {
                child_exec(&p->commands[i], prev_fd, pipefd[1]);
            }
        } else if (pid < 0) {
            perror("fork");
            return 1;
        } else {
            if (shell_is_interactive && !p->background && i == 0) {
                setpgid(pid, shell_pgid);
            }
            pids[num_pids++] = pid;

            if (prev_fd != STDIN_FILENO) close(prev_fd);
            if (i < num_cmds - 1) close(pipefd[1]);
            prev_fd = pipefd[0];
        }
    }

    if (p->background) {
        pipeline_t *p_copy = malloc(sizeof(pipeline_t));
        pipeline_deep_copy(p_copy, p);
        if (num_pids > 0) {
            add_job(pids[0], p_copy, p->cmd_str ? strdup(p->cmd_str) : NULL);
        }
        return 0;
    }

    for (int i = 0; i < num_pids; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == num_pids - 1) {
            if (WIFEXITED(status)) last_exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) last_exit_code = 128 + WTERMSIG(status);
        }
    }

    return last_exit_code;
}

int execute_builtin(char **args) {
    if (!args || !args[0]) return 0;

    if (strcmp(args[0], "cd") == 0) {
        if (!args[1] || strcmp(args[1], "~") == 0) {
            char *home = getenv("HOME");
            if (!home) home = getpwuid(getuid())->pw_dir;
            if (chdir(home) != 0) { perror("cd"); return 1; }
        } else if (strcmp(args[1], "-") == 0) {
            char *oldpwd = getenv("OLDPWD");
            if (!oldpwd) { fprintf(stderr, "cd: OLDPWD not set\n"); return 1; }
            if (chdir(oldpwd) != 0) { perror("cd"); return 1; }
            printf("%s\n", oldpwd);
        } else {
            if (chdir(args[1]) != 0) { perror("cd"); return 1; }
        }
        char cwd[MAX_LINE];
        if (getcwd(cwd, sizeof(cwd))) setenv("OLDPWD", getenv("PWD"), 1);
        if (getcwd(cwd, sizeof(cwd))) setenv("PWD", cwd, 1);
        return 0;
    }

    if (strcmp(args[0], "exit") == 0) {
        int code = args[1] ? atoi(args[1]) : last_exit_code;
        save_history();
        exit(code);
    }

    if (strcmp(args[0], "export") == 0) {
        if (!args[1]) {
            for (char **e = environ; *e; e++) printf("declare -x %s\n", *e);
            return 0;
        }
        for (int i = 1; args[i]; i++) {
            char *eq = strchr(args[i], '=');
            if (eq) {
                *eq = '\0';
                setenv(args[i], eq + 1, 1);
                *eq = '=';
            } else {
                setenv(args[i], "", 1);
            }
        }
        return 0;
    }

    if (strcmp(args[0], "unset") == 0) {
        for (int i = 1; args[i]; i++) unsetenv(args[i]);
        return 0;
    }

    if (strcmp(args[0], "alias") == 0) {
        if (!args[1]) {
            alias_t *a = aliases;
            while (a) { printf("alias %s='%s'\n", a->name, a->value); a = a->next; }
            return 0;
        }
        for (int i = 1; args[i]; i++) {
            char *eq = strchr(args[i], '=');
            if (eq) {
                *eq = '\0';
                char *val = eq + 1;
                size_t vlen = strlen(val);
                if (vlen >= 2 && ((val[0] == '\'' && val[vlen-1] == '\'') || (val[0] == '"' && val[vlen-1] == '"'))) {
                    val[vlen-1] = '\0';
                    val++;
                }
                alias_t *existing = aliases;
                while (existing) {
                    if (strcmp(existing->name, args[i]) == 0) {
                        free(existing->value);
                        existing->value = strdup(val);
                        break;
                    }
                    existing = existing->next;
                }
                if (!existing) {
                    alias_t *a = malloc(sizeof(alias_t));
                    a->name = strdup(args[i]);
                    a->value = strdup(val);
                    a->next = aliases;
                    aliases = a;
                }
                *eq = '=';
            } else {
                alias_t *a = aliases;
                while (a) {
                    if (strcmp(a->name, args[i]) == 0) { printf("alias %s='%s'\n", a->name, a->value); break; }
                    a = a->next;
                }
                if (!a) fprintf(stderr, "alias: %s not found\n", args[i]);
            }
        }
        return 0;
    }

    if (strcmp(args[0], "unalias") == 0) {
        for (int i = 1; args[i]; i++) {
            alias_t *prev = NULL, *curr = aliases;
            while (curr) {
                if (strcmp(curr->name, args[i]) == 0) {
                    if (prev) prev->next = curr->next;
                    else aliases = curr->next;
                    free(curr->name); free(curr->value); free(curr);
                    break;
                }
                prev = curr;
                curr = curr->next;
            }
        }
        return 0;
    }

    if (strcmp(args[0], "echo") == 0) {
        int newline = 1;
        int start = 1;
        if (args[1] && strcmp(args[1], "-n") == 0) { newline = 0; start = 2; }
        for (int i = start; args[i]; i++) {
            printf("%s%s", args[i], args[i+1] ? " " : "");
        }
        if (newline) printf("\n");
        fflush(stdout);
        return 0;
    }

    if (strcmp(args[0], "pwd") == 0) {
        char cwd[MAX_LINE];
        if (getcwd(cwd, sizeof(cwd))) printf("%s\n", cwd);
        else perror("pwd");
        return 0;
    }

    if (strcmp(args[0], "type") == 0) {
        if (!args[1]) { fprintf(stderr, "type: missing argument\n"); return 1; }
        for (int i = 1; args[i]; i++) {
            if (is_builtin(args[i])) { printf("%s is a shell builtin\n", args[i]); continue; }
            alias_t *a = aliases;
            int found = 0;
            while (a) { if (strcmp(a->name, args[i]) == 0) { printf("%s is aliased to '%s'\n", args[i], a->value); found = 1; break; } a = a->next; }
            if (found) continue;
            char *path = getenv("PATH");
            if (path) {
                char *path_copy = strdup(path);
                char *dir = strtok(path_copy, ":");
                while (dir) {
                    char full[MAX_LINE];
                    snprintf(full, sizeof(full), "%s/%s", dir, args[i]);
                    if (access(full, X_OK) == 0) { printf("%s is %s\n", args[i], full); break; }
                    dir = strtok(NULL, ":");
                }
                free(path_copy);
                if (!dir) fprintf(stderr, "type: %s not found\n", args[i]);
            }
        }
        return 0;
    }

    if (strcmp(args[0], "source") == 0 || strcmp(args[0], ".") == 0) {
        if (!args[1]) { fprintf(stderr, "source: missing filename\n"); return 1; }
        return execute_script(args[1]);
    }

    if (strcmp(args[0], "history") == 0) {
        HIST_ENTRY **h = history_list();
        if (h) {
            for (int i = 0; h[i]; i++) printf("%5d  %s\n", i + history_base, h[i]->line);
        }
        return 0;
    }

    if (strcmp(args[0], "jobs") == 0) return builtin_jobs(args);
    if (strcmp(args[0], "fg") == 0) return builtin_fg(args);
    if (strcmp(args[0], "bg") == 0) return builtin_bg(args);

    return 127;
}

int is_builtin(char *cmd) {
    if (!cmd) return 0;
    static const char *builtins[] = {
        "cd", "exit", "export", "unset", "alias", "unalias",
        "echo", "pwd", "type", "source", ".",
        "history", "jobs", "fg", "bg", NULL
    };
    for (int i = 0; builtins[i]; i++) {
        if (strcmp(cmd, builtins[i]) == 0) return 1;
    }
    return 0;
}
