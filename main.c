#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_COMMAND_LENGTH 1024
#define MAX_ARGS 64
#define MAX_ARG_LENGTH 256
#define DELIMETERS " \t\r\n\a"

void print_prompt() {
    printf("sell-shell> ");
}

void read_command(char *cmd) {
    fgets(cmd, MAX_COMMAND_LENGTH, stdin);
}

void parse_command(char *cmd, char **args) {
    int index = 0;
    char *token = strtok(cmd, DELIMETERS);
    while (token != NULL) {
        args[index++] = token;
        token = strtok(NULL, DELIMETERS);
    }
    args[index] = NULL;
}

int is_background(char **args) {
    int i = 0;
    while (args[i] != NULL) {
        if (strcmp(args[i], "&") == 0) {
            args[i] = NULL;
            return 1;
        }
        i++;
    }
    return 0;
}

void execute_command(char **args, int background) {
    pid_t pid = fork();
    if (pid == 0) {  
        if (execvp(args[0], args) == -1) {
            perror("simple-shell");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("simple-shell");
    } else {  
        if (!background) {
            int status;
            waitpid(pid, &status, 0);
        }
    }
}

int main() {
    char cmd[MAX_COMMAND_LENGTH];
    char *args[MAX_ARGS];
    int background;

    while (1) {
        print_prompt();
        read_command(cmd);
        if (strlen(cmd) == 1 && cmd[0] == '\n') {
            continue;
        }
        parse_command(cmd, args);
        background = is_background(args);
        execute_command(args, background);
    }
    
    return 0;
}
