#include "sell-shell.h"

job_t *job_list = NULL;
int next_job_id = 1;

void init_jobs(void) {
    job_list = NULL;
    next_job_id = 1;
}

int add_job(pid_t pgid, pipeline_t *p, char *cmd_str) {
    job_t *j = malloc(sizeof(job_t));
    j->id = next_job_id++;
    j->pgid = pgid;
    j->pipeline = *p;
    j->cmd_str = cmd_str;
    j->status = JOB_RUNNING;
    j->next = job_list;
    job_list = j;

    printf("[%d] %d\n", j->id, pgid);
    free(p);
    return j->id;
}

int remove_job(int id) {
    job_t *prev = NULL, *curr = job_list;
    while (curr) {
        if (curr->id == id) {
            if (prev) prev->next = curr->next;
            else job_list = curr->next;
            free_pipeline(&curr->pipeline);
            free(curr->cmd_str);
            free(curr);
            return 1;
        }
        prev = curr;
        curr = curr->next;
    }
    return 0;
}

job_t *find_job(int id) {
    job_t *j = job_list;
    while (j) { if (j->id == id) return j; j = j->next; }
    return NULL;
}

job_t *find_job_by_pid(pid_t pid) {
    job_t *j = job_list;
    while (j) { if (j->pgid == pid) return j; j = j->next; }
    return NULL;
}

job_t *find_stopped_job(void) {
    job_t *j = job_list;
    while (j) { if (j->status == JOB_STOPPED) return j; j = j->next; }
    return NULL;
}

void update_jobs(void) {
    job_t *prev = NULL, *curr = job_list;
    while (curr) {
        int status;
        pid_t result = waitpid(curr->pgid, &status, WNOHANG | WUNTRACED | WCONTINUED);
        if (result == 0) {
            /* still running or stopped */
            if (WIFSTOPPED(status)) curr->status = JOB_STOPPED;
            prev = curr;
            curr = curr->next;
        } else if (result == curr->pgid || result > 0) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                printf("\n[%d] Done %s\n", curr->id, curr->cmd_str ? curr->cmd_str : "");
                job_t *to_remove = curr;
                curr = curr->next;
                if (prev) prev->next = to_remove->next;
                else job_list = to_remove->next;
                free_pipeline(&to_remove->pipeline);
                free(to_remove->cmd_str);
                free(to_remove);
            } else if (WIFSTOPPED(status)) {
                curr->status = JOB_STOPPED;
                printf("\n[%d] Stopped %s\n", curr->id, curr->cmd_str ? curr->cmd_str : "");
                prev = curr;
                curr = curr->next;
            } else if (WIFCONTINUED(status)) {
                curr->status = JOB_RUNNING;
                prev = curr;
                curr = curr->next;
            } else {
                prev = curr;
                curr = curr->next;
            }
        } else {
            /* waitpid failed, assume job is gone */
            if (errno == ECHILD) {
                job_t *to_remove = curr;
                curr = curr->next;
                if (prev) prev->next = to_remove->next;
                else job_list = to_remove->next;
                free_pipeline(&to_remove->pipeline);
                free(to_remove->cmd_str);
                free(to_remove);
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
    }
}

int builtin_jobs(char **args) {
    (void)args;
    update_jobs();
    job_t *j = job_list;
    while (j) {
        const char *status_str = j->status == JOB_RUNNING ? "Running" : "Stopped";
        printf("[%d] %s %s\n", j->id, status_str, j->cmd_str ? j->cmd_str : "");
        j = j->next;
    }
    return 0;
}

int builtin_fg(char **args) {
    update_jobs();
    job_t *j = NULL;

    if (args[1]) {
        if (args[1][0] == '%') {
            int id = atoi(args[1] + 1);
            j = find_job(id);
        } else {
            int id = atoi(args[1]);
            j = find_job(id);
        }
    } else {
        j = find_stopped_job();
    }

    if (!j) {
        fprintf(stderr, "fg: no such job\n");
        return 1;
    }

    if (j->pipeline.cmd_str) printf("%s\n", j->pipeline.cmd_str);
    tcsetpgrp(shell_terminal, j->pgid);

    if (j->status == JOB_STOPPED) {
        kill(-j->pgid, SIGCONT);
    }

    int status;
    waitpid(j->pgid, &status, WUNTRACED);

    tcsetpgrp(shell_terminal, shell_pgid);

    if (WIFSTOPPED(status)) {
        j->status = JOB_STOPPED;
    } else {
        remove_job(j->id);
    }

    return 0;
}

int builtin_bg(char **args) {
    update_jobs();
    job_t *j = NULL;

    if (args[1]) {
        if (args[1][0] == '%') {
            int id = atoi(args[1] + 1);
            j = find_job(id);
        } else {
            int id = atoi(args[1]);
            j = find_job(id);
        }
    } else {
        j = find_stopped_job();
    }

    if (!j) {
        fprintf(stderr, "bg: no such job\n");
        return 1;
    }

    if (j->pipeline.cmd_str) printf("[%d] %s &\n", j->id, j->pipeline.cmd_str);
    j->status = JOB_RUNNING;
    kill(-j->pgid, SIGCONT);
    return 0;
}
