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
#include <dirent.h>
#include <termios.h>
#include <sys/stat.h>

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

// Helper function to check if path is directory
int is_directory(const char *path) {
				struct stat statbuf;
				if (stat(path, &statbuf) != 0)
								return 0;
				return S_ISDIR(statbuf.st_mode);
}

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

struct completion_result {
		char** matches;
		int count;
};

int str_in_array(char* str, char** array, int count) {
		for(int i = 0; i < count; i++) {
				if(strcmp(str, array[i]) == 0) {
						return 1;
				}
		}
		return 0;
}

struct completion_result find_matching_commands(const char* partial) {
		DIR* dir;
		struct dirent* entry;
		char* path;
		char* dir_path;
		size_t partial_len = strlen(partial);

		// Initialize result struct
		struct completion_result result;
		result.matches = malloc(100 * sizeof(char*));
		result.count = 0;

		if (partial_len == 0) {
				return result;
		}

		// Check for "cd " prefix
		int is_cd = (strncmp(partial, "cd ", 3) == 0);
		const char* search_term = is_cd ? partial + 3 : partial;

		// Check for path component
		char* last_slash = strrchr(search_term, '/');
		char* search_dir;

		if (last_slash) {
				// If there's a slash, search in that directory
				size_t dir_len = last_slash - search_term + 1;
				search_dir = strndup(search_term, dir_len);
				search_term = last_slash + 1;
				dir = opendir(search_dir);
		} else {
				// If no slash, search in current directory
				search_dir = ".";
				dir = opendir(".");
		}

		if (dir) {
				while ((entry = readdir(dir)) && result.count < 100) {
						// Skip . and .. entries
						if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
								continue;
						}

						// Only show directories for cd command
						if (is_cd) {
								if (entry->d_type == DT_DIR &&
									strncmp(entry->d_name, search_term, strlen(search_term)) == 0) {
										char* match;
										if (last_slash) {
												size_t match_len = strlen(search_dir) + strlen(entry->d_name) + 2;
												match = malloc(match_len);
												snprintf(match, match_len, "cd %s%s", search_dir, entry->d_name);
										} else {
												size_t match_len = strlen(entry->d_name) + 4; // "cd " + name
												match = malloc(match_len);
												snprintf(match, match_len, "cd %s", entry->d_name);
										}
										if (!str_in_array(match, result.matches, result.count)) {
												result.matches[result.count++] = match;
										} else {
												free(match);
										}
								}
						} else {
								if (strncmp(entry->d_name, search_term, strlen(search_term)) == 0 &&
										entry->d_type != DT_DIR) { // Don't include directories if not cd
										char* match;
										if (last_slash) {
												size_t match_len = strlen(search_dir) + strlen(entry->d_name) + 2;
												match = malloc(match_len);
												snprintf(match, match_len, "%s%s", search_dir, entry->d_name);
										} else {
												match = strdup(entry->d_name);
										}
										if (!str_in_array(match, result.matches, result.count)) {
												result.matches[result.count++] = match;
										} else {
												free(match);
										}
								}
						}
				}
				closedir(dir);
		}

		if (last_slash && search_dir != NULL) {
				free(search_dir);
		}

		// Only search PATH for executables if not doing cd completion
		if (!is_cd && !last_slash) {
				path = getenv("PATH");
				if (path) {
						char* path_copy = strdup(path);
						dir_path = strtok(path_copy, ":");

						while (dir_path != NULL && result.count < 100) {
								dir = opendir(dir_path);
								if (dir) {
										while ((entry = readdir(dir)) && result.count < 100) {
												if (strncmp(entry->d_name, partial, partial_len) == 0 &&
														entry->d_type != DT_DIR) { // Skip directories in PATH
														if (!str_in_array(entry->d_name, result.matches, result.count)) {
																result.matches[result.count++] = strdup(entry->d_name);
														}
												}
										}
										closedir(dir);
								}
								dir_path = strtok(NULL, ":");
						}
						free(path_copy);
				}
		}

		return result;
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
				if (chdir(args[1]) != 0) {
						perror("lsh");
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
		int cursor_pos = 0;
		char *buffer = malloc(sizeof(char) * bufsize);
		int c;
		struct termios old_termios, new_termios;

		if (!buffer) {
				fprintf(stderr, "lsh: allocation error\n");
				exit(EXIT_FAILURE);
		}

		// Get terminal settings
		tcgetattr(STDIN_FILENO, &old_termios);
		new_termios = old_termios;
		new_termios.c_lflag &= ~(ICANON | ECHO); // Disable canonical mode and echo
		tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

		while (1) {
				c = getchar();

				if (c == EOF || c == '\n') {
						buffer[position] = '\0';
						tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
						printf("\n");
						return buffer;
				} else if (c == 127) { // Backspace
						if (cursor_pos > 0) {
								for(int i = cursor_pos - 1; i < position - 1; i++) {
										buffer[i] = buffer[i + 1];
								}
								position--;
								cursor_pos--;
								printf("\b");
								for(int i = cursor_pos; i < position; i++) {
										printf("%c", buffer[i]);
								}
								printf(" \r❯ ");
								for(int i = 0; i < position; i++) {
										printf("%c", buffer[i]);
								}
								for(int i = position; i > cursor_pos; i--) {
										printf("\b");
								}
						}
				} else if (c == 27) { // Escape sequence
						char seq[3];
						seq[0] = getchar();
						if (seq[0] == '[') {
								seq[1] = getchar();
								if (seq[1] == 'C') { // Right arrow
										if (cursor_pos < position) {
												cursor_pos++;
												printf("\033[1C");
										}
								} else if (seq[1] == 'D') { // Left arrow
										if (cursor_pos > 0) {
												cursor_pos--;
												printf("\033[1D");
										}
								}
						}
				} else if (c == '\t') {
						buffer[position] = '\0';
						struct completion_result completions = find_matching_commands(buffer);

						if (completions.count == 1) {
								// Single match - complete it
								strncpy(buffer, completions.matches[0], bufsize - 1);
								buffer[bufsize - 1] = '\0';
								position = strlen(buffer);
								cursor_pos = position;
								printf("\r%s❯%s %s", COLOR_BLUE, COLOR_RESET, buffer);
						} else if (completions.count > 1) {
								// Multiple matches - show them all
								printf("\n");
								for (int i = 0; i < completions.count; i++) {
										if (strncmp(buffer, "cd ", 3) == 0 && is_directory(completions.matches[i])) {
												printf("%s/  ", completions.matches[i]);
										} else {
												printf("%s  ", completions.matches[i]);
										}
								}
								printf("\n%s❯%s %s", COLOR_BLUE, COLOR_RESET, buffer);
						}
						fflush(stdout);

						// Clean up
						for (int i = 0; i < completions.count; i++) {
								free(completions.matches[i]);
						}
						free(completions.matches);
				} else {
						// Insert character at cursor position
						for(int i = position; i > cursor_pos; i--) {
								buffer[i] = buffer[i-1];
						}
						buffer[cursor_pos] = c;
						position++;
						cursor_pos++;

						// Redraw line from cursor position
						for(int i = cursor_pos-1; i < position; i++) {
								printf("%c", buffer[i]);
						}
						// Move cursor back to current position
						for(int i = position; i > cursor_pos; i--) {
								printf("\b");
						}
				}

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
