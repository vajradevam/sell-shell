#ifndef SELL_SHELL_H
#define SELL_SHELL_H

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <dirent.h>
#include <errno.h>
#include <glob.h>
#include <ctype.h>
#include <time.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_LINE 8192
#define MAX_ARGS 512
#define MAX_ALIASES 256
#define MAX_JOBS 64
#define HISTORY_FILE ".sell-shell_history"
#define MAX_HISTORY 5000

extern char **environ;

#define REDIR_NONE      0
#define REDIR_IN        1
#define REDIR_OUT       2
#define REDIR_APPEND    3
#define REDIR_ERR       4
#define REDIR_ERR_APPEND 5
#define REDIR_ALL       6

#define JOB_RUNNING     0
#define JOB_STOPPED     1
#define JOB_DONE        2

typedef struct {
    int type;
    char *file;
    int fd;
} redir_t;

typedef struct {
    char **args;
    redir_t *redirects;
    int num_redirects;
} command_t;

typedef struct {
    command_t *commands;
    int num_commands;
    int background;
    char *cmd_str;
} pipeline_t;

typedef struct job {
    int id;
    pid_t pgid;
    pipeline_t pipeline;
    char *cmd_str;
    int status;
    struct job *next;
} job_t;

typedef struct alias {
    char *name;
    char *value;
    struct alias *next;
} alias_t;

extern alias_t *aliases;
extern job_t *job_list;
extern int last_exit_code;
extern int shell_terminal;
extern int shell_pgid;
extern int shell_is_interactive;
extern char *history_file_path;

pipeline_t parse_line(char *line);
void free_pipeline(pipeline_t *p);
int execute_pipeline(pipeline_t *p);
int execute_builtin(char **args);
int is_builtin(char *cmd);
void init_jobs(void);
int add_job(pid_t pgid, pipeline_t *p, char *cmd_str);
int remove_job(int id);
job_t *find_job(int id);
job_t *find_job_by_pid(pid_t pid);
job_t *find_stopped_job(void);
void update_jobs(void);
int builtin_jobs(char **args);
int builtin_fg(char **args);
int builtin_bg(char **args);
char *generate_prompt(void);
void init_completion(void);
int execute_script_line(const char *line);
int execute_script(const char *filename);
void setup_signals(void);
void load_history(void);
void save_history(void);
char *expand_variables(const char *word);
char *expand_variables_internal(const char *word, int in_double_quote);

#endif
