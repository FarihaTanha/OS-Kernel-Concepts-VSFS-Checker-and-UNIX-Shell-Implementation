#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_LINE 1024       // Max command line length
#define MAX_ARGS 64         // Max number of arguments
#define MAX_HISTORY 20      // Max number of commands in history

// Global variables
char history[MAX_HISTORY][MAX_LINE];
int history_count = 0;
int running_cmd = 0;

// Signal handler for CTRL+C
void sigint_handler(int sig) {
    if (running_cmd) {
        printf("\nTerminating current command...\n");
    } else {
        printf("\nsh> ");
        fflush(stdout);
    }
}

// Function to add command to history
void add_to_history(char *cmd) {
    if (strlen(cmd) == 0 || cmd[0] == '\n')
        return;
        
    // Remove newline character if present
    if (cmd[strlen(cmd) - 1] == '\n')
        cmd[strlen(cmd) - 1] = '\0';
        
    if (history_count < MAX_HISTORY) {
        strcpy(history[history_count++], cmd);
    } else {
        // Shift history to make room for new command
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            strcpy(history[i], history[i + 1]);
        }
        strcpy(history[MAX_HISTORY - 1], cmd);
    }
}

// Function to display command history
void display_history() {
    printf("Command History:\n");
    for (int i = 0; i < history_count; i++) {
        printf("%d: %s\n", i + 1, history[i]);
    }
}

// Function to parse command line into arguments
int parse_line(char *line, char **args) {
    int argc = 0;
    char *token = strtok(line, " \t\n");
    
    while (token != NULL && argc < MAX_ARGS - 1) {
        args[argc++] = token;
        token = strtok(NULL, " \t\n");
    }
    
    args[argc] = NULL;  // Null terminate the array
    return argc;
}

// Function to execute a command with redirection
void execute_command(char **args) {
    int in_redirect = 0, out_redirect = 0, out_append = 0;
    char *infile = NULL, *outfile = NULL;
    
    // Check for redirection symbols
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            in_redirect = 1;
            infile = args[i+1];
            args[i] = NULL;  // Remove redirection symbols from args
        } else if (strcmp(args[i], ">") == 0) {
            out_redirect = 1;
            outfile = args[i+1];
            args[i] = NULL;
        } else if (strcmp(args[i], ">>") == 0) {
            out_append = 1;
            outfile = args[i+1];
            args[i] = NULL;
        }
    }
    
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("Fork failed");
        exit(1);
    } else if (pid == 0) {  // Child process
        // Handle input redirection
        if (in_redirect) {
            int fd = open(infile, O_RDONLY);
            if (fd < 0) {
                perror("Failed to open input file");
                exit(1);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        
        // Handle output redirection
        if (out_redirect) {
            int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror("Failed to open output file");
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        } else if (out_append) {
            int fd = open(outfile, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) {
                perror("Failed to open output file");
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        
        // Execute the command
        if (execvp(args[0], args) < 0) {
            printf("Command not found: %s\n", args[0]);
            exit(1);
        }
    } else {  // Parent process
        running_cmd = 1;
        int status;
        waitpid(pid, &status, 0);
        running_cmd = 0;
    }
}

// Function to handle piping between commands
void handle_pipes(char **args, int argc) {
    int i, j, k;
    int pipe_count = 0;
    int cmd_count = 1;
    int pipe_pos[MAX_ARGS];
    
    // Count pipes and their positions
    for (i = 0; i < argc; i++) {
        if (strcmp(args[i], "|") == 0) {
            pipe_pos[pipe_count++] = i;
            cmd_count++;
        }
    }
    
    if (pipe_count == 0) {
        // No pipes, just execute the command
        execute_command(args);
        return;
    }
    
    // Create pipe arrays
    int pipes[pipe_count][2];
    for (i = 0; i < pipe_count; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("Pipe creation failed");
            return;
        }
    }
    
    // Execute commands with pipes
    pid_t pid;
    int cmd_start = 0;
    
    // For each command in the pipeline
    for (i = 0; i < cmd_count; i++) {
        // Parse command arguments
        char *cmd_args[MAX_ARGS];
        int cmd_end = (i < cmd_count - 1) ? pipe_pos[i] : argc;
        
        for (j = cmd_start, k = 0; j < cmd_end; j++, k++) {
            cmd_args[k] = args[j];
        }
        cmd_args[k] = NULL;
        
        pid = fork();
        
        if (pid < 0) {
            perror("Fork failed");
            exit(1);
        } else if (pid == 0) {  // Child process
            // Set up pipes
            if (i > 0) {  // Not the first command
                // Get input from the previous pipe
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            
            if (i < cmd_count - 1) {  // Not the last command
                // Send output to the next pipe
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            
            // Close all pipe file descriptors
            for (j = 0; j < pipe_count; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            // Execute the command
            if (execvp(cmd_args[0], cmd_args) < 0) {
                printf("Command not found: %s\n", cmd_args[0]);
                exit(1);
            }
        }
        
        // Update command start position for the next command
        cmd_start = pipe_pos[i] + 1;
    }
    
    // Parent process
    running_cmd = 1;
    
    // Close all pipe file descriptors in the parent
    for (i = 0; i < pipe_count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    // Wait for all child processes to finish
    for (i = 0; i < cmd_count; i++) {
        wait(NULL);
    }
    
    running_cmd = 0;
}

// Function to handle multiple commands separated by semicolons
void handle_multiple_commands(char *line) {
    char *cmd = strtok(line, ";");
    
    while (cmd != NULL) {
        // Remove leading and trailing whitespace
        while (*cmd == ' ' || *cmd == '\t') cmd++;
        
        if (strlen(cmd) > 0) {
            add_to_history(cmd);
            
            // Handle the case where there's already a newline at the end
            if (cmd[strlen(cmd) - 1] == '\n')
                cmd[strlen(cmd) - 1] = '\0';
                
            // Execute this command
            char *args[MAX_ARGS];
            int argc = parse_line(cmd, args);
            
            if (argc > 0) {
                // Check for built-in commands
                if (strcmp(args[0], "exit") == 0) {
                    printf("Exiting shell...\n");
                    exit(0);
                } else if (strcmp(args[0], "cd") == 0) {
                    if (args[1] == NULL) {
                        // Change to home directory
                        chdir(getenv("HOME"));
                    } else {
                        if (chdir(args[1]) != 0) {
                            perror("cd failed");
                        }
                    }
                } else if (strcmp(args[0], "history") == 0) {
                    display_history();
                } else {
                    // Check for pipes
                    int has_pipe = 0;
                    for (int i = 0; i < argc; i++) {
                        if (strcmp(args[i], "|") == 0) {
                            has_pipe = 1;
                            break;
                        }
                    }
                    
                    if (has_pipe) {
                        handle_pipes(args, argc);
                    } else {
                        execute_command(args);
                    }
                }
            }
        }
        
        cmd = strtok(NULL, ";");
    }
}

// Function to handle logical operators (&&)
void handle_logical_operators(char *line) {
    char *cmd;
    char *saveptr;
    char line_copy[MAX_LINE];
    strcpy(line_copy, line);
    
    cmd = strtok_r(line_copy, "&&", &saveptr);
    
    while (cmd != NULL) {
        // Remove leading and trailing whitespace
        while (*cmd == ' ' || *cmd == '\t') cmd++;
        
        if (strlen(cmd) > 0) {
            // Execute this command and check its return value
            char cmd_copy[MAX_LINE];
            strcpy(cmd_copy, cmd);
            handle_multiple_commands(cmd_copy);
            
            // If the command fails, break the chain
            if (WEXITSTATUS(0) != 0) {
                break;
            }
        }
        
        cmd = strtok_r(NULL, "&&", &saveptr);
    }
}

int main() {
    char line[MAX_LINE];
    
    // Set up signal handler for CTRL+C
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    
    printf("Simple UNIX Shell\n");
    
    while (1) {
        printf("sh> ");
        fflush(stdout);
        
        if (fgets(line, MAX_LINE, stdin) == NULL) {
            // Handle EOF (Ctrl+D)
            printf("\nExiting shell...\n");
            break;
        }
        
        // Skip empty lines
        if (strlen(line) <= 1) continue;
        
        // Check for logical operators
        if (strstr(line, "&&") != NULL) {
            handle_logical_operators(line);
        } else {
            handle_multiple_commands(line);
        }
    }
    
    return 0;
}
