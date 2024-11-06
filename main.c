/***************************************************************************
    file         main.c
    author       LandonAL
    date         Tuesday, 2 November 2024
    brief        LSH (Libstephen SHell)
***************************************************************************/

#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <termios.h>
#include <sys/stat.h>
#include <glob.h>

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

int is_directory(const char *path) {
	struct stat statbuf;

	if (stat(path, &statbuf) != 0) {
		return 0;
	}

	return S_ISDIR(statbuf.st_mode);
}

/*
    Function declarations for builtin shell commands.
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
	for (int i = 0; i < count; i++) {
		if (strcmp(str, array[i]) == 0) {
			return 1;
		}
	}

	return 0;
}

struct completion_result find_matching_commands(const char* partial, int is_arg) {
	struct completion_result result;
	result.matches = malloc(100 * sizeof(char*));
	result.count = 0;

	if (strlen(partial) == 0) {
		return result;
	}

	// Only do glob pattern matching for arguments
	if (is_arg) {
		glob_t globbuf;
		char* pattern = malloc(strlen(partial) + 2);

		strcpy(pattern, partial);
		strcat(pattern, "*");

		int glob_flags = GLOB_TILDE | GLOB_MARK;

		if (glob(pattern, glob_flags, NULL, &globbuf) == 0) {
			for (size_t i = 0; i < globbuf.gl_pathc && result.count < 100; i++) {
				// Check for duplicates before adding
				if (!str_in_array(globbuf.gl_pathv[i], result.matches, result.count)) {
					result.matches[result.count++] = strdup(globbuf.gl_pathv[i]);
				}
			}
		}

		free(pattern);
		globfree(&globbuf);

		return result;
	}

	// For command completion, search PATH
	if (strchr(partial, '/') == NULL) {
		char* path = getenv("PATH");

		if (path) {
			char* path_copy = strdup(path);
			char* dir = strtok(path_copy, ":");

			while (dir != NULL && result.count < 100) {
				DIR* d = opendir(dir);

				if (d) {
					struct dirent* entry;

					while ((entry = readdir(d)) && result.count < 100) {
						if (strncmp(entry->d_name, partial, strlen(partial)) == 0) {
							char* full_path = malloc(strlen(dir) + strlen(entry->d_name) + 2);
							sprintf(full_path, "%s/%s", dir, entry->d_name);

							if (access(full_path, X_OK) == 0) {
								// Check for duplicates before adding
								if (!str_in_array(entry->d_name, result.matches, result.count)) {
									result.matches[result.count++] = strdup(entry->d_name);
								}
							}

							free(full_path);
						}
					}

					closedir(d);
				}

				dir = strtok(NULL, ":");
			}

			free(path_copy);
		}
	}

	return result;
}

/*
    Builtin function implementations.
*/

int lsh_cd(char **args)
{
	if (args[1] == NULL) {
		// If no argument is provided, go to home directory
		char *homedir = getenv("HOME");

		if (homedir != NULL) {
			if (chdir(homedir) != 0) {
				perror("lsh");
			}
		} else {
			fprintf(stderr, "lsh: HOME not set\n");
		}
	} else {
		char *path = args[1];

		if (path[0] == '~') {
			char *homedir = getenv("HOME");

			if (homedir != NULL) {
				char *new_path = malloc(strlen(homedir) + strlen(path));
				strcpy(new_path, homedir);
				strcat(new_path, path + 1);

				if (chdir(new_path) != 0) {
					perror("lsh");
				}

				free(new_path);
			} else {
				fprintf(stderr, "lsh: HOME not set\n");
			}
		} else if (chdir(args[1]) != 0) {
			perror("lsh");
		}
	}

	return 1;
}

int lsh_help(char **args)
{
	int i;
	printf("Type program names and arguments, and hit enter.\n");
	printf("The following are built in:\n");

	for (i = 0; i < lsh_num_builtins(); i++) {
		printf("  %s\n", builtin_str[i]);
	}

	printf("Use the man command for information on other programs.\n");
	return 1;
}

int lsh_exit(char **args)
{
	return 0;
}

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
				for (int i = cursor_pos - 1; i < position - 1; i++) {
					buffer[i] = buffer[i + 1];
				}

				position--;
				cursor_pos--;
				printf("\b");

				for(int i = cursor_pos; i < position; i++) {
					printf("%c", buffer[i]);
				}

				printf(" \r%s❯%s%s ", COLOR_BLUE, COLOR_WHITE, COLOR_WHITE);

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
			char *last_space = strrchr(buffer, ' ');
			int is_arg = (last_space != NULL);
			char *completion_str = is_arg ? last_space + 1 : buffer;

			struct completion_result completions = find_matching_commands(completion_str, is_arg);

			if (completions.count == 1) {
				if (is_arg) {
					// Replace only the part after the last space
					strcpy(last_space + 1, completions.matches[0]);
					position = strlen(buffer);
				} else {
					strcpy(buffer, completions.matches[0]);
					position = strlen(buffer);
				}

				cursor_pos = position;
				printf("\r%s❯%s%s %s", COLOR_BLUE, COLOR_WHITE, COLOR_WHITE, buffer);
			} else if (completions.count > 1) {
				printf("\n");

				for (int i = 0; i < completions.count; i++) {
					printf("%s%s%s  ", is_directory(completions.matches[i]) ? COLOR_BLUE : "",
					completions.matches[i], COLOR_RESET);
				}

				printf("\n%s❯%s%s %s", COLOR_BLUE, COLOR_WHITE, COLOR_WHITE, buffer);
			}

			fflush(stdout);

			// Clean up
			for (int i = 0; i < completions.count; i++) {
				free(completions.matches[i]);
			}

			free(completions.matches);
		} else {
			// Insert character at cursor position
			for (int i = position; i > cursor_pos; i--) {
				buffer[i] = buffer[i-1];
			}

			buffer[cursor_pos] = c;
			position++;
			cursor_pos++;

			// Redraw line from cursor position
			for (int i = cursor_pos-1; i < position; i++) {
				printf("%c", buffer[i]);
			}

			// Move cursor back to current position
			for (int i = position; i > cursor_pos; i--) {
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

void lsh_loop(void)
{
	char *line;
	char **args;
	int status;

	do {
		printf("%s❯%s%s ", COLOR_BLUE, COLOR_WHITE, COLOR_WHITE);
		line = lsh_read_line();
		args = lsh_split_line(line);
		status = lsh_execute(args);

		free(line);
		free(args);
	} while (status);
}

int main(int argc, char **argv)
{
	// Load config files, if any.

	// Run command loop.
	lsh_loop();

	// Perform any shutdown/cleanup.

	return EXIT_SUCCESS;
}
