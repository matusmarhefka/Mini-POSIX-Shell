/* shell.c - Mini POSIX Shell
 *
 * Author: Matus Marhefka
 * Date:   2015-04-23
 *
 *
 * IMPLEMENTATION:
 * Shell creates 3 threads:
 * 1) Input handling thread
 * 2) Execution handling thread
 * 3) Signal handling thread
 * Input handling thread is processing the user input which is then
 * used in execution handling thread (monitor is used for synchronization)
 * which forks the new process and then executes the user input. Input
 * and execution threads have all signals blocked so only signal handling
 * thread can receive signals delivered to the main shell process.
 *
 *
 * Mini POSIX Shell features:
 * -- file redirection using >FILE or <FILE
 * -- run process in background by specifying '&' character
 *    at the end of the command line
 *
 * Mini POSIX Shell built-in commands:
 * -- jobs - prints all background jobs
 * -- cd   - change working directory
 * -- exit - exits the shell
 *
 */

#define _POSIX_C_SOURCE 200809L
#ifndef _REENTRANT
#  define _REENTRANT
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include "shell.h"


int is_space(char c)
{
	if (c == ' ' || c == '\t')
		return 1;
	return 0;
}

/* Sets exit_flag to the value specified as the argument. */
void set_exit_flag(int flag)
{
	pthread_mutex_lock(&mtx_exit);
	exit_flag = flag;
	pthread_mutex_unlock(&mtx_exit);
}

/* Returns 1 if exec thread should exit, otherwise 0. */
int is_exit_flag(void)
{
	int flag;

	pthread_mutex_lock(&mtx_exit);
	flag = exit_flag;
	pthread_mutex_unlock(&mtx_exit);

	return flag;
}

/* Input thread monitor: allows to execute content in args. */
void monitor_args_execute(void)
{
	pthread_mutex_lock(&mtx);
	exec_args = 1;
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mtx);
}

/* Input thread monitor: waits on exec thread to finish execution. */
void monitor_args_wait_finished(void)
{
	pthread_mutex_lock(&mtx);
	while (exec_args != 0)
		pthread_cond_wait(&cond, &mtx);
	pthread_mutex_unlock(&mtx);
}

/* Exec thread monitor: waits on input thread to fill in the args. */
void monitor_args_wait_executable(void)
{
	pthread_mutex_lock(&mtx);
	while (exec_args != 1)
		pthread_cond_wait(&cond, &mtx);
	pthread_mutex_unlock(&mtx);
}

/* Exec thread monitor: allows input thread to work with args. */
void monitor_args_executed(void)
{
	pthread_mutex_lock(&mtx);
	exec_args = 0;
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mtx);
}

/* Processes shell input and fills the global variable args in format suitable
 * for execvp() function. Returns:
 *  0 - input processed and filled args
 *  1 - input processing error
 * -1 - memory allocation error
 */
int create_args(char *buf)
{
	int i, j;
	char *ptr, *bptr;

	run_bg = 0;

	/* skip beginning whitespaces */
	bptr = ptr = buf;
	while (is_space(*ptr)) {
		ptr++;
		bptr = ptr;
	}
	/* preprocessing: count the number of arguments including
	 * filename (argsc) */
	argsc = 1;
	i = 0;
	while (*ptr != '\0') {
		if (is_space(*ptr)) {
			/* argument too long */
			if (i >= MAXARG) {
				fprintf(stderr, "Argument too long!\n");
				return 1;
			}
			/* IO redirection */
			if (*bptr == '>' || *bptr == '<') {
				bptr++;
				if (is_space(*bptr)) {
					fprintf(stderr, "No whitespaces after "
					        "'%c' operator!\n", *(--bptr));
					return 1;
				}

			} else if (*bptr == '&') {
				run_bg = 1;
				bptr--;
				while (is_space(*bptr))
					bptr--;
				bptr++;
				*bptr = '\0';
				argsc--;
			} else {
				argsc++;
			}
			i = -1;
			bptr = ptr + 1;

			/* skip whitespaces */
			while (is_space(*ptr)) {
				ptr++;
				bptr = ptr;
			}
			ptr--;
		}
		ptr++;
		i++;
	}
	/* cases where we have only one argument and it is too long */
	if (i >= MAXARG) {
		fprintf(stderr, "Argument too long!\n");
		return 1;
	}
	/* cases when the last argument has redirection operator */
	if ((argsc > 1) && (*bptr == '>' || *bptr == '<'))
		argsc--;
	/* background job, line ends with '&' so we remove it */
	if (argsc > 1 && *bptr == '&') {
		run_bg = 1;
		bptr--;
		while (is_space(*bptr))
			bptr--;
		bptr++;
		*bptr = '\0';
		argsc--;
	} else { /* removes trailing whitespace */
		ptr--;
		while (is_space(*ptr))
			ptr--;
		ptr++;
		*ptr = '\0';
	}

	/* args must be NULL terminated, we must count also NULL member */
	argsc++;


	/* allocate memory for arguments */
	args = calloc(argsc, sizeof(char *));
	if (args == NULL) {
		fprintf(stderr, "Not enough memory!\n");
		return -1;
	}
	for (i = 0; i < argsc-1; i++) {
		args[i] = calloc(MAXARG, sizeof(char));
		if (args[i] == NULL) {
			fprintf(stderr, "Not enough memory!\n");
			return -1;
		}
	}

	/* fills in the args variable with shell input */
	bptr = ptr = buf;
	while (is_space(*ptr)) {
		ptr++;
		bptr = ptr;
	}
	i = j = 0;
	while (*ptr != '\0') {
		if (is_space(*ptr)) {
			if (*bptr == '>') {
				bptr++;
				memcpy(redir_t, bptr, j);
				redir_t[j-1] = '\0';
			} else if (*bptr == '<') {
				bptr++;
				memcpy(redir_f, bptr, j);
				redir_f[j-1] = '\0';
			} else {
				memcpy(args[i], bptr, j);
				i++;
			}
			j = -1;
			bptr = ptr + 1;

			/* skip whitespaces */
			while (is_space(*ptr)) {
				ptr++;
				bptr = ptr;
			}
			ptr--;
		}
		ptr++;
		j++;
	}
	/* last argument or only one argument */
	if (*bptr == '>') {
		bptr++;
		memcpy(redir_t, bptr, j);
		redir_t[j-1] = '\0';
		args[i] = NULL;
	} else if (*bptr == '<') {
		bptr++;
		memcpy(redir_f, bptr, j);
		redir_f[j-1] = '\0';
		args[i] = NULL;
	} else {
		memcpy(args[i], bptr, j);
		args[i+1] = NULL;
	}

	return 0;
}

/* Frees the memory occupied by args. */
void clear_args(void)
{
	int i;

	for (i = 0; i < argsc; i++)
		free(args[i]);
	free(args);

	redir_t[0] = '\0';
	redir_f[0] = '\0';
}

/* Flushes the rest of an input on stdin if maximum length of input
 * is reached. */
void flush_input(void)
{
	int rv, n;
	char c;

	while ((n = read(STDIN_FILENO, &c, 1)) != 0) {
		if (n < 0) {
			if (errno == EINTR)
				continue;
			else {
				perror("read");
				fflush(stderr);
				/* signal exec thread to exit */
				set_exit_flag(1);
				monitor_args_execute();
				rv = 1;
				pthread_exit(&rv);
			}
		}
		if (c == '\n')
			break;
	}
}

/* Input thread */
void *input_start(void *arg)
{
	int rv;
	ssize_t n;
	char cmd_buf[MAXLEN];

	printf("$ ");
	fflush(stdout);

	/* read from stdin */
	while ((n = read(STDIN_FILENO, &cmd_buf, MAXLEN)) != 0) {
		if (n < 0) {
			perror("read");
			fflush(stderr);
			/* signal exec thread to exit */
			set_exit_flag(1);
			monitor_args_execute();
			return (void *)1;
		}

		if (n == MAXLEN) {
			if (cmd_buf[n-1] == '\n') {
				fprintf(stderr, "Argument too long!\n");
				printf("$ ");
			} else {
				flush_input();
				fprintf(stderr, "Argument too long!\n");
				printf("$ ");
			}
			fflush(stdout);
			fflush(stderr);
			continue;
		}

		cmd_buf[n-1] = '\0';

		/* constructs args variable for execvp */
		rv = create_args(cmd_buf);
		if (rv == -1) {
			fflush(stderr);
			break;
		}
		if (rv == 1) {
			fflush(stderr);
			printf("$ ");
			fflush(stdout);
			continue;
		}

		if (strcmp(args[0], "exit") == 0) {
			clear_args();
			/* signal exec thread to exit */
			set_exit_flag(1);
			monitor_args_execute();
			return 0;
		}
		if (strcmp(args[0], "jobs") == 0) {
			clear_args();
			jobs_print(&jobs);
			printf("$ ");
			fflush(stdout);
			continue;
		}
		if (strcmp(args[0], "cd") == 0) {
			clear_args();
			if (change_cwd() == -1) {
				fflush(stderr);
				/* signal exec thread to exit */
				set_exit_flag(1);
				monitor_args_execute();
				return 0;
			}
			printf("$ ");
			fflush(stdout);
			fflush(stderr);
			continue;
		}
		if (strlen(args[0]) == 0) {
			clear_args();
			printf("\r$ ");
			fflush(stdout);
			continue;
		}

		/* signal the exec thread to execute the args content */
		monitor_args_execute();
		/* wait until execution is finished */
		monitor_args_wait_finished();

		clear_args();
		if (is_exit_flag())
			return 0;

		printf("$ ");
		fflush(stdout);
	}

	printf("\n");
	fflush(stdout);

	/* signal exec thread to exit */
	set_exit_flag(1);
	monitor_args_execute();

	return 0;
}

/* If target argument is STDOUT_FILENO (STDIN_FILENO) the stdout (stdin)
 * descriptor is redirected into the file specified in global variable
 * redir_t (redir_f). On success, the file descriptor of redirection file
 * is returned, otherwise -1 indicating error. */
int redir_file(int target)
{
	int fd;

	if (target == STDOUT_FILENO) {
		fd = open(redir_t, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR);
		if (fd == -1) {
			perror("open");
			return -1;
		}

		/* make stdout go to fd */
		if (dup2(fd, STDOUT_FILENO) == -1) {
			perror("dup2");
			return -1;
		}
	} else {
		fd = open(redir_f, O_CREAT|O_RDONLY, S_IRUSR|S_IWUSR);
		if (fd == -1) {
			perror("open");
			return -1;
		}

		/* make stdin go to fd */
		if (dup2(fd, STDIN_FILENO) == -1) {
			perror("dup2");
			return -1;
		}
	}

	return fd;
}

/* Executes the file in args[0] and also handles file redirection
 * and backgrounding of processes. Returns 0 on success or -1 on error. */
int execute_file(void)
{
	pid_t cpid, w;
	sigset_t signal_set;
	int status, fd;
	
	cpid = fork();
	if (cpid == -1) {
		perror("fork");
		return -1;
	}
	if (cpid == 0) {  /* child */
		/* unblock all signals for new process except SIGTSTP */
		sigfillset(&signal_set);
		sigdelset(&signal_set, SIGTSTP);
		if (run_bg)
			sigdelset(&signal_set, SIGINT);
		pthread_sigmask(SIG_UNBLOCK, &signal_set, NULL);

		/* IO redirection */
		if (redir_t[0] != '\0') {
			fd = redir_file(STDOUT_FILENO);
			if (fd == -1)
				return -1;
		}
		if (redir_f[0] != '\0') {
			fd = redir_file(STDIN_FILENO);
			if (fd == -1)
				return -1;
		}

		if (run_bg) {
			/* child make itself the process group leader (of its
			 * own group - different from shell group) - this
			 * will lead in SIGTTIN signal when trying to read
			 * from stdin which causes stopping of child */
			if (setpgid(0, 0) == -1) {
				perror("setpgid");
				exit(1);
			}
		}

		/* PATH variable is searched automatically */
		execvp(args[0], args);
		if (errno == ENOENT)
			fprintf(stderr, "%s: command not found...\n", args[0]);
		else
			perror("");
		exit(1);
	} else {  /* parent */
		/* restore all signals blocking */
		sigfillset(&signal_set);
		pthread_sigmask(SIG_BLOCK, &signal_set, NULL);

		if (run_bg) {
			if (jobs_insert(&jobs, args[0], cpid) == -1)
				return -1;
			printf("[%d] %s\n", cpid, args[0]);
			fflush(stdout);
		} else {
			w = waitpid(cpid, &status, 0);
			if (w == -1 && errno != ECHILD) {
				perror("waitpid");
				return -1;
			} else if (w > 0) {
				if (WIFSIGNALED(status))
					printf("\n");
			}
		}
	}

	return 0;
}

/* Exec thread */
void *cmd_exec_start(void *arg)
{
	for(;;) {
		/* wait until input thread fills in the args */
		monitor_args_wait_executable();
		if (is_exit_flag())
			break;

		if (execute_file() == -1) {
			set_exit_flag(1);
			fflush(stderr);
			monitor_args_executed();
			return (void *)1;
		}

		/* allow input thread to work with the args */
		monitor_args_executed();
	}

	return 0;
}

/* Prints the exit status of a background process. */
void print_status(int w, int status)
{
	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) != 0)
			printf("\n[%d]+ Exit %d\n", w, WEXITSTATUS(status));
		else
			printf("\n[%d]+ Done\n", w);
	} else if (WIFSIGNALED(status)) {
		printf("\n[%d]+ Killed\n", w);
	} else if (WIFSTOPPED(status)) {
		printf("\n[%d]+ Stopped\n", w);
	} else {
		printf("\n[%d]+ Terminated\n", w);
	}
}

/* Signal handling thread */
void *sig_handler(void *arg)
{
	sigset_t signal_set;
	pid_t w;
	int sig, status;

	for (;;) {
		/* wait for any signal */
		sigfillset(&signal_set);
		sigwait(&signal_set, &sig);

		/* signal caught */
		switch (sig) {
			case SIGINT:  /* ctrl+c */
				pthread_mutex_lock(&mtx);
				if (exec_args)
					printf("\n");
				else
					printf("\n$ ");
				pthread_mutex_unlock(&mtx);
				fflush(stdout);
				break;
			case SIGTSTP: /* ctrl+z */
				pthread_mutex_lock(&mtx);
				if (exec_args)
					printf("\n");
				else
					printf("\n$ ");
				pthread_mutex_unlock(&mtx);
				fflush(stdout);
				break;
			case SIGCHLD: /* child exit */
				w = waitpid(-1, &status, WNOHANG);
				if (w > 0) {
					if (jobs_find_remove(&jobs, w)) {
						print_status(w, status);
						pthread_mutex_lock(&mtx);
						if (!exec_args)
							printf("$ ");
						pthread_mutex_unlock(&mtx);
						fflush(stdout);
					}
				}
				break;
			case SIGUSR1:
				if (is_exit_flag())
					return 0;
				break;
			default:
				break;
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int stat, i;
	pthread_t threads[3];
	pthread_attr_t attr;
	sigset_t signal_set;

	stat = pthread_attr_init(&attr);
	if (stat != 0)
		handle_error_en(stat, "pthread_attr_init");
	stat = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	if (stat != 0)
		handle_error_en(stat, "pthread_attr_setdetachstate");

	/* initilize our jobs (struct job_list) variable - a linked
	 * list for storing backgrounded jobs */
	stat = jobs_init(&jobs);
	if (stat != 0)
		handle_error_en(stat, "jobs_init: pthread_mutex_init");

	/* make shell process group leader */
	if (setpgid(getpid(), getpid()) == -1) {
		perror("setpgid");
		exit(1);
	}
	/* set the terminal prcess group to the shell process group - causes
	 * that only processes in shell group can read stdin, if process
	 * of other process group tries to read from stdin, it will be
	 * sent the SIGTTIN signal which stops that process */
	if (tcsetpgrp(STDIN_FILENO, getpgid(0)) == -1) {
		perror("tcsetpgrp");
		exit(1);
	}

	/* block all signals */
	sigfillset(&signal_set);
	stat = pthread_sigmask(SIG_BLOCK, &signal_set, NULL);
	if (stat != 0)
		handle_error_en(stat, "pthread_sigmask");
	/* create the signal handling thread */
	stat = pthread_create(&threads[0], &attr, sig_handler, NULL);
	if (stat != 0)
		handle_error_en(stat, "pthread_create");

	/* input thread */
	stat = pthread_create(&threads[1], &attr, input_start, NULL);
	if (stat != 0)
		handle_error_en(stat, "pthread_create");

	/* commands executing thread */
	stat = pthread_create(&threads[2], &attr, cmd_exec_start, NULL);
	if (stat != 0)
		handle_error_en(stat, "pthread_create");

	/* Free attribute and wait for the input and exec threads */
	stat = pthread_attr_destroy(&attr);
	if (stat != 0)
		handle_error_en(stat, "pthread_attr_destroy");
	for (i = 1; i < 3; i++) {
		stat = pthread_join(threads[i], NULL);
		if (stat != 0)
			handle_error_en(stat, "pthread_join");
	}

	/* finally kill and join our signal handling thread */
	stat = pthread_kill(threads[0], SIGUSR1);
	if (stat != 0)
		handle_error_en(stat, "pthread_kill");
	stat = pthread_join(threads[0], NULL);
	if (stat != 0)
		handle_error_en(stat, "pthread_join");

	jobs_free(&jobs);
	exit(0);
}
