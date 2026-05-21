#include "sell-shell.h"

static char *builtin_names[] = {
    "cd", "exit", "export", "unset", "alias", "unalias",
    "echo", "pwd", "type", "source",
    "history", "jobs", "fg", "bg", NULL
};

static char *command_generator(const char *text, int state) {
    static int builtin_index;
    static char **path_dirs;
    static int num_dirs;
    static int dir_index;
    static DIR *dir;
    static struct dirent *entry;
    static size_t text_len;

    if (!state) {
        builtin_index = 0;
        dir_index = 0;
        dir = NULL;
        text_len = strlen(text);

        char *path = getenv("PATH");
        if (path) {
            path_dirs = malloc(MAX_ARGS * sizeof(char *));
            num_dirs = 0;
            char *path_copy = strdup(path);
            char *dir_name = strtok(path_copy, ":");
            while (dir_name && num_dirs < MAX_ARGS - 1) {
                path_dirs[num_dirs++] = strdup(dir_name);
                dir_name = strtok(NULL, ":");
            }
            free(path_copy);
            path_dirs[num_dirs] = NULL;
        } else {
            path_dirs = NULL;
            num_dirs = 0;
        }
    }

    /* Try builtins first */
    while (builtin_names[builtin_index]) {
        char *name = builtin_names[builtin_index++];
        if (strncmp(name, text, text_len) == 0) {
            return strdup(name);
        }
    }

    /* Try directories from PATH */
    while (dir || path_dirs[dir_index]) {
        if (!dir) {
            dir = opendir(path_dirs[dir_index]);
            if (!dir) { dir_index++; continue; }
        }
        while ((entry = readdir(dir))) {
            if (entry->d_name[0] == '.') continue;
            if (strncmp(entry->d_name, text, text_len) == 0) {
                struct stat st;
                char full[MAX_LINE];
                snprintf(full, sizeof(full), "%s/%s", path_dirs[dir_index], entry->d_name);
                if (stat(full, &st) == 0 && (st.st_mode & S_IXUSR)) {
                    return strdup(entry->d_name);
                }
            }
        }
        closedir(dir);
        dir = NULL;
        dir_index++;
    }

    for (int i = 0; path_dirs && path_dirs[i]; i++) free(path_dirs[i]);
    free(path_dirs);
    path_dirs = NULL;
    return NULL;
}

static char *filename_generator(const char *text, int state) {
    static DIR *dir;
    static struct dirent *entry;
    static size_t text_len;
    static char *dir_path, *prefix;

    if (!state) {
        text_len = strlen(text);
        const char *slash = strrchr(text, '/');
        if (slash) {
            dir_path = strndup(text, slash - text + 1);
            prefix = strdup(slash + 1);
        } else {
            dir_path = strdup("./");
            prefix = strdup(text);
        }
        dir = opendir(dir_path[0] ? dir_path : ".");
        if (!dir) {
            free(dir_path);
            free(prefix);
            return NULL;
        }
    }

    while ((entry = readdir(dir))) {
        char *name = entry->d_name;
        if (name[0] == '.' && prefix[0] != '.') continue;
        if (strncmp(name, prefix, text_len) == 0) {
            char *result;
            if (dir_path[0] == '.' && dir_path[1] == '/' && strlen(dir_path) == 2) {
                result = strdup(name);
            } else {
                result = malloc(strlen(dir_path) + strlen(name) + 1);
                strcpy(result, dir_path);
                strcat(result, name);
            }
            return result;
        }
    }

    closedir(dir);
    dir = NULL;
    free(dir_path);
    free(prefix);
    return NULL;
}

static char **completion_function(const char *text, int start, int end) {
    (void)end;
    rl_attempted_completion_over = 1;
    if (start == 0) {
        return rl_completion_matches(text, command_generator);
    }
    return rl_completion_matches(text, filename_generator);
}

static char *variable_generator(const char *text, int state) {
    static int env_index;
    static size_t text_len;

    if (!state) {
        env_index = 0;
        text_len = strlen(text);
    }

    while (environ[env_index]) {
        char *eq = strchr(environ[env_index], '=');
        if (eq) {
            size_t name_len = eq - environ[env_index];
            if (name_len >= text_len && strncmp(environ[env_index], text, text_len) == 0) {
                return strndup(environ[env_index], name_len);
            }
        }
        env_index++;
    }
    return NULL;
}

static int dynamic_complete(int count, int key) {
    (void)count;
    (void)key;
    /* Check if completing a variable */
    int pos = rl_point;
    while (pos > 0 && rl_line_buffer[pos - 1] != ' ' && rl_line_buffer[pos - 1] != '$') pos--;
    if (pos > 0 && rl_line_buffer[pos - 1] == '$') {
        /* Complete variable name */
        char **matches;
        rl_attempted_completion_over = 1;
        rl_completion_entry_function = variable_generator;
        matches = rl_completion_matches(rl_line_buffer + pos, variable_generator);
        if (matches) {
            rl_completion_matches(rl_line_buffer + pos, variable_generator);
        }
        return 0;
    }
    return rl_complete_internal('?');
}

void init_completion(void) {
    rl_attempted_completion_function = completion_function;

    /* Bind tab to our custom completer */
    rl_bind_keyseq("\\t", dynamic_complete);
}
