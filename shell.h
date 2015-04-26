/* shell.h - Mini POSIX Shell
 *
 * Author: Matus Marhefka
 * Date:   2015-04-23
 *
 * Global variables declarations/definitions, jobs and cd commands
 * implementation.
 *
 */

#ifndef JOBS_H
#define JOBS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define MAXLEN 513
#define MAXARG 256

#define handle_error_en(en, msg) \
	do { errno = en; perror(msg); exit(1); } while (0)

struct job_item {
	int pid;
	char name[MAXARG];
	struct job_item *next;
};

struct job_list {
	struct job_item *first;
	pthread_mutex_t jmtx;
};

/* count of arguments on command line */
int argsc;
/* array of argument strings ending with NULL element (for execvp) */
char **args;
/* variables exec_args, mtx and cond are forming our monitor for sync.
 * between input and execution threads */
volatile int exec_args;
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/* used for storing filenames for IO redirection */
char redir_t[MAXARG];
char redir_f[MAXARG];

/* indicates exit if set, protected by mtx_exit mutex */
volatile int exit_flag;
pthread_mutex_t mtx_exit = PTHREAD_MUTEX_INITIALIZER;

/* stores jobs running in background */
struct job_list jobs;
/* background flag: if set process is launched in background */
volatile int run_bg;


/* Changes the current working directory. */
int change_cwd(void)
{
	int argc = argsc - 1;  /* don't count the trailing NULL in args */

	if (argc != 2) {
		fprintf(stderr, "cd: one argument required\n");
		return 1;
	}

	if (chdir(args[1]) == -1) {
		if (errno == ENOENT || errno == ENOTDIR) {
			fprintf(stderr, "cd: %s: No such directory\n", args[1]);
			return 1;
		}
		return -1;
	}

	return 0;
}

/* Initializes job_list structure and mutexe variable. Returns 0 on success,
 * error code (of pthread_mutex_init) on error. */
int jobs_init(struct job_list *list)
{
	int rc;

	rc = pthread_mutex_init(&(list->jmtx), NULL);
	list->first = NULL;

	return rc;
}

/* Frees the memory occupied by the job_list structure. */
void jobs_free(struct job_list *list)
{
	struct job_item *it;

	pthread_mutex_lock(&(list->jmtx));
	if (list->first != NULL) {
		it = list->first;
		while (it != NULL) {
			list->first = list->first->next;
			free(it);
			it = list->first;
		}
		list->first = NULL;
	}
	pthread_mutex_unlock(&(list->jmtx));

	pthread_mutex_destroy(&(list->jmtx));
}

/* Inserts job with name and pid into the job_list. */
int jobs_insert(struct job_list *list, char *name, int pid)
{
	struct job_item *it;

	pthread_mutex_lock(&(list->jmtx));
	it = malloc(sizeof(struct job_item));
	if (it == NULL) {
		fprintf(stderr, "Could not allocate memory for job\n");
		pthread_mutex_unlock(&(list->jmtx));
		return -1;
	}
	it->pid = pid;
	strcpy(it->name, name);
	it->next = list->first;
	list->first = it;
	pthread_mutex_unlock(&(list->jmtx));

	return 0;
}

/* Finds and removes job from the job_list. Returns 1 if job is found
 * and removed, 0 otherwise. */
int jobs_find_remove(struct job_list *list, int pid)
{
	struct job_item *it, *prev;

	pthread_mutex_lock(&(list->jmtx));
	if (list->first != NULL) {
		it = prev = list->first;
		while (it != NULL) {
			if (it->pid == pid) {
				if (it == list->first)
					list->first = it->next;
				/* remove job_item from list */
				prev->next = it->next;
				free(it);
				pthread_mutex_unlock(&(list->jmtx));
				return 1;
			}
			prev = it;
			it = it->next;
		}
	}
	pthread_mutex_unlock(&(list->jmtx));

	return 0;
}

/* Prints all background jobs on the stdout. */
void jobs_print(struct job_list *list)
{
	struct job_item *it;

	pthread_mutex_lock(&(list->jmtx));
	if (list->first != NULL) {
		it = list->first;
		while (it != NULL) {
			printf("[%d] %s\n", it->pid, it->name);
			it = it->next;
		}
	}
	pthread_mutex_unlock(&(list->jmtx));
}

#endif /* SHELL_H */
