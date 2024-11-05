/***************************************************************************//**
		@file         main.c
		@author       LandonAL
		@date         Tuesday, 2 November 2024
		@brief        LSH (Libstephen SHell)
*******************************************************************************/

#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ANSI color codes
#define COLOR_BLACK "\033[0;30m"
#define COLOR_RED "\033[0;31m"
#define COLOR_GREEN "\033[0;32m"
#define COLOR_YELLOW "\033[0;33m"
#define COLOR_BLUE "\033[0;34m"
#define COLOR_MAGENTA "\033[0;35m"
#define COLOR_CYAN "\033[0;36m"
#define COLOR_WHITE "\033[0;37m"
#define COLOR_BRIGHT_BLACK "\033[1;30m"
#define COLOR_BRIGHT_RED "\033[1;31m"
#define COLOR_BRIGHT_GREEN "\033[1;32m"
#define COLOR_BRIGHT_YELLOW "\033[1;33m"
#define COLOR_BRIGHT_BLUE "\033[1;34m"
#define COLOR_BRIGHT_MAGENTA "\033[1;35m"
#define COLOR_BRIGHT_CYAN "\033[1;36m"
#define COLOR_BRIGHT_WHITE "\033[1;37m"
#define COLOR_RESET "\033[0m"

/*
		Function Declarations for builtin shell commands:
	*/
int lsh_cd(char **args);
int lsh_help(char **args);
int lsh_exit(char **args);

/*
		List of builtin commands, followed by their corresponding functions.
	*/
char *builtin_str[] = {
		"cd",
		"help",
		"exit"
};

int (*builtin_func[]) (char **) = {
		&lsh_cd,
		&lsh_help,
		&lsh_exit
};

int lsh_num_builtins() {
		return sizeof(builtin_str) / sizeof(char *);
}

/*
		Builtin function implementations.
*/

/**
			@brief Builtin command: change directory.
			@param args List of args.  args[0] is "cd".  args[1] is the directory.
			@return Always returns 1, to continue executing.
	*/
int lsh_cd(char **args)
{
		if (args[1] == NULL) {
				fprintf(stderr, "lsh: expected argument to \"cd\"\n");
		} else {
				char *path = args[1];
				if (path[0] == '~') {
						char *home = getenv("HOME");
						if (home != NULL) {
								char *new_path = malloc(strlen(home) + strlen(path));
								strcpy(new_path, home);
								strcat(new_path, path + 1);
								if (chdir(new_path) != 0) {
										perror("lsh");
								}
								free(new_path);
						}
				} else {
						if (chdir(path) != 0) {
								perror("lsh");
						}
				}
		}
		return 1;
}

/**
			@brief Builtin command: print help.
			@param args List of args.  Not examined.
			@return Always returns 1, to continue executing.
	*/
int lsh_help(char **args)
{
		int i;
		printf("Stephen Brennan's LSH\n");
		printf("Type program names and arguments, and hit enter.\n");
		printf("The following are built in:\n");

		for (i = 0; i < lsh_num_builtins(); i++) {
				printf("  %s\n", builtin_str[i]);
		}

		printf("Use the man command for information on other programs.\n");
		return 1;
}

/**
			@brief Builtin command: exit.
			@param args List of args.  Not examined.
			@return Always returns 0, to terminate execution.
	*/
int lsh_exit(char **args)
{
		return 0;
}

/**
		@brief Launch a program and wait for it to terminate.
		@param args Null terminated list of arguments (including program).
		@return Always returns 1, to continue execution.
	*/
int lsh_launch(char **args)
{
		pid_t pid;
		int status;

		pid = fork();
		if (pid == 0) {
				// Child process
				if (execvp(args[0], args) == -1) {
						perror("lsh");
				}
				exit(EXIT_FAILURE);
		} else if (pid < 0) {
				// Error forking
				perror("lsh");
		} else {
				// Parent process
				do {
						waitpid(pid, &status, WUNTRACED);
				} while (!WIFEXITED(status) && !WIFSIGNALED(status));
		}

		return 1;
}

/**
			@brief Execute shell built-in or launch program.
			@param args Null terminated list of arguments.
			@return 1 if the shell should continue running, 0 if it should terminate
	*/
int lsh_execute(char **args)
{
		int i;

		if (args[0] == NULL) {
				// An empty command was entered.
				return 1;
		}

		for (i = 0; i < lsh_num_builtins(); i++) {
				if (strcmp(args[0], builtin_str[i]) == 0) {
						return (*builtin_func[i])(args);
				}
		}

		return lsh_launch(args);
}

/**
			@brief Read a line of input from stdin.
			@return The line from stdin.
	*/
char *lsh_read_line(void)
{
#ifdef LSH_USE_STD_GETLINE
		char *line = NULL;
		ssize_t bufsize = 0; // have getline allocate a buffer for us
		if (getline(&line, &bufsize, stdin) == -1) {
				if (feof(stdin)) {
						exit(EXIT_SUCCESS);  // We received an EOF
				} else  {
						perror("lsh: getline\n");
						exit(EXIT_FAILURE);
				}
		}
		return line;
#else
#define LSH_RL_BUFSIZE 1024
		int bufsize = LSH_RL_BUFSIZE;
		int position = 0;
		char *buffer = malloc(sizeof(char) * bufsize);
		int c;

		if (!buffer) {
				fprintf(stderr, "lsh: allocation error\n");
				exit(EXIT_FAILURE);
		}

		while (1) {
				// Read a character
				c = getchar();

				if (c == EOF) {
						exit(EXIT_SUCCESS);
				} else if (c == '\n') {
						buffer[position] = '\0';
						return buffer;
				} else {
						buffer[position] = c;
				}
				position++;

				// If we have exceeded the buffer, reallocate.
				if (position >= bufsize) {
						bufsize += LSH_RL_BUFSIZE;
						buffer = realloc(buffer, bufsize);
						if (!buffer) {
								fprintf(stderr, "lsh: allocation error\n");
								exit(EXIT_FAILURE);
						}
				}
		}
#endif
}

#define LSH_TOK_BUFSIZE 64
#define LSH_TOK_DELIM " \t\r\n\a"
/**
			@brief Split a line into tokens (very naively).
			@param line The line.
			@return Null-terminated array of tokens.
	*/
char **lsh_split_line(char *line)
{
		int bufsize = LSH_TOK_BUFSIZE, position = 0;
		char **tokens = malloc(bufsize * sizeof(char*));
		char *token, **tokens_backup;

		if (!tokens) {
				fprintf(stderr, "lsh: allocation error\n");
				exit(EXIT_FAILURE);
		}

		token = strtok(line, LSH_TOK_DELIM);
		while (token != NULL) {
				tokens[position] = token;
				position++;

				if (position >= bufsize) {
						bufsize += LSH_TOK_BUFSIZE;
						tokens_backup = tokens;
						tokens = realloc(tokens, bufsize * sizeof(char*));
						if (!tokens) {
		free(tokens_backup);
								fprintf(stderr, "lsh: allocation error\n");
								exit(EXIT_FAILURE);
						}
				}

				token = strtok(NULL, LSH_TOK_DELIM);
		}
		tokens[position] = NULL;
		return tokens;
}

/**
			@brief Loop getting input and executing it.
	*/
void lsh_loop(void)
{
		char *line;
		char **args;
		int status;

		do {
				printf("%s❯%s ", COLOR_BLUE, COLOR_RESET);
				line = lsh_read_line();
				args = lsh_split_line(line);
				status = lsh_execute(args);

				free(line);
				free(args);
		} while (status);
}

/**
			@brief Main entry point.
			@param argc Argument count.
			@param argv Argument vector.
			@return status code
	*/
int main(int argc, char **argv)
{
		// Load config files, if any.

		// Run command loop.
		lsh_loop();

		// Perform any shutdown/cleanup.

		return EXIT_SUCCESS;
}
