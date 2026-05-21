#include "sell-shell.h"

alias_t *aliases = NULL;
int shell_terminal;
int shell_pgid;
int shell_is_interactive = 0;
char *history_file_path = NULL;

static void sigint_handler(int sig) {
    (void)sig;
    last_exit_code = 130;
    printf("\n");
    rl_on_new_line();
    rl_replace_line("", 0);
    rl_redisplay();
}

static void sigtstp_handler(int sig) {
    (void)sig;
    /* Move the shell process group back to foreground */
    tcsetpgrp(shell_terminal, shell_pgid);
    signal(SIGTSTP, SIG_DFL);
    raise(SIGTSTP);
}

static void sigcont_handler(int sig) {
    (void)sig;
    signal(SIGTSTP, sigtstp_handler);
    tcsetpgrp(shell_terminal, shell_pgid);
}

void setup_signals(void) {
    struct sigaction sa_int = {0};
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_int, NULL);

    struct sigaction sa_quit = {0};
    sa_quit.sa_handler = SIG_IGN;
    sigaction(SIGQUIT, &sa_quit, NULL);

    struct sigaction sa_tstp = {0};
    sa_tstp.sa_handler = sigtstp_handler;
    sigemptyset(&sa_tstp.sa_mask);
    sigaction(SIGTSTP, &sa_tstp, NULL);

    struct sigaction sa_cont = {0};
    sa_cont.sa_handler = sigcont_handler;
    sigemptyset(&sa_cont.sa_mask);
    sigaction(SIGCONT, &sa_cont, NULL);

    struct sigaction sa_chld = {0};
    sa_chld.sa_handler = SIG_DFL;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    sigaction(SIGCHLD, &sa_chld, NULL);
}

void load_history(void) {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (!home) return;

    size_t path_len = strlen(home) + strlen(HISTORY_FILE) + 2;
    history_file_path = malloc(path_len);
    snprintf(history_file_path, path_len, "%s/%s", home, HISTORY_FILE);

    read_history(history_file_path);
    stifle_history(MAX_HISTORY);
}

void save_history(void) {
    if (history_file_path) {
        append_history(MAX_HISTORY, history_file_path);
    }
}

int main(int argc, char *argv[]) {
    shell_terminal = STDIN_FILENO;
    shell_is_interactive = isatty(shell_terminal);

    if (shell_is_interactive) {
        while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);

        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);

        shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) < 0) {
            perror("setpgid");
            exit(1);
        }
        tcsetpgrp(shell_terminal, shell_pgid);
    }

    setup_signals();
    init_jobs();
    load_history();
    init_completion();

    /* Script mode: run file, then exit */
    if (argc > 1) {
        int result = execute_script(argv[1]);
        save_history();
        exit(result);
    }

    /* Interactive mode */
    printf("sell-shell v2.0 — type 'exit' to quit\n");

    char *line;
    while (1) {
        if (shell_is_interactive) {
            char *prompt_str = generate_prompt();
            line = readline(prompt_str);
            if (!line) { printf("\n"); break; }
            add_history(line);
        } else {
            char buf[MAX_LINE];
            if (!fgets(buf, sizeof(buf), stdin)) break;
            size_t len = strlen(buf);
            while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
            line = strdup(buf);
        }

        if (!line) break;

        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        if (*trimmed == '\0' || *trimmed == '#') {
            free(line);
            continue;
        }

        update_jobs();

        pipeline_t pipeline = parse_line(trimmed);
        execute_pipeline(&pipeline);
        free_pipeline(&pipeline);

        free(line);
    }

    save_history();
    return 0;
}
