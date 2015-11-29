#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <execinfo.h>

#include "common.h"
#include "appconfig.h"
#include "log.h"
#include "web_client.h"
#include "plugins_d.h"
#include "rrd.h"
#include "popen.h"
#include "main.h"
#include "daemon.h"

#define BACKTRACE_SIZE 4096

void print_backtrace()
{
	void *buffer[BACKTRACE_SIZE];
	int nptrs;

	nptrs = backtrace(buffer, BACKTRACE_SIZE);
	fprintf(stderr, "\n\nSTACK TRACE (%d addresses):\n\n", nptrs);
	backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);
	fprintf(stderr, "\n\n");
}

void sig_handler(int signo)
{
	switch(signo) {
		case SIGILL:
		case SIGABRT:
		case SIGFPE:
		case SIGSEGV:
		case SIGBUS:
		case SIGSYS:
		case SIGTRAP:
		case SIGXCPU:
		case SIGXFSZ:
			info("Death signaled exit (signal %d). Errno: %d (%s)", signo, errno, strerror(errno));
			print_backtrace();
			signal(signo, SIG_DFL);
			break;

		case SIGKILL:
		case SIGTERM:
		case SIGQUIT:
		case SIGINT:
		case SIGHUP:
		case SIGUSR1:
		case SIGUSR2:
			info("Signaled exit (signal %d). Errno: %d (%s)", signo, errno, strerror(errno));
			print_backtrace();
			signal(SIGPIPE, SIG_IGN);
			signal(SIGTERM, SIG_IGN);
			signal(SIGQUIT, SIG_IGN);
			signal(SIGHUP,  SIG_IGN);
			signal(SIGINT,  SIG_IGN);
			signal(SIGCHLD, SIG_IGN);
			kill_childs();
			rrdset_free_all();
			//unlink("/var/run/netdata.pid");
			info("NetData exiting. Bye bye...");
			exit(1);
			break;

		case SIGPIPE:
			info("Signaled PIPE (signal %d). Errno: %d (%s)", signo, errno, strerror(errno));
			// this is received when web clients send a reset
			// no need to log it.
			// info("Ignoring signal %d. Errno: %d (%s)", signo, errno, strerror(errno));
			break;

		default:
			info("Signal %d received. Falling back to default action for it.", signo);
			signal(signo, SIG_DFL);
			break;
	}
}

char rundir[FILENAME_MAX + 1] = "/var/run/netdata";
char pidfile[FILENAME_MAX + 1] = "";
void prepare_rundir() {
	if(getuid() != 0) {
		mkdir("/run/user", 0775);
		snprintf(rundir, FILENAME_MAX, "/run/user/%d", getuid());
		mkdir(rundir, 0775);
		snprintf(rundir, FILENAME_MAX, "/run/user/%d/netdata", getuid());
	}

	snprintf(pidfile, FILENAME_MAX, "%s/netdata.pid", rundir);

	if(mkdir(rundir, 0775) != 0) {
		if(errno != EEXIST) fprintf(stderr, "Cannot create directory '%s' (%s).", rundir, strerror(errno));
	}
}

int become_user(const char *username)
{
	struct passwd *pw = getpwnam(username);
	if(!pw) {
		error("User %s is not present. Error: %s\n", username, strerror(errno));
		return -1;
	}

	if(chown(rundir, pw->pw_uid, pw->pw_gid) != 0) {
		error("Cannot chown directory '%s' to user %s. Error: %s\n", rundir, username, strerror(errno));
		return -1;
	}

	if(setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) != 0) {
		error("Cannot switch to user's %s group (gid: %d). Error: %s\n", username, pw->pw_gid, strerror(errno));
		return -1;
	}

	if(setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) != 0) {
		error("Cannot switch to user %s (uid: %d). Error: %s\n", username, pw->pw_uid, strerror(errno));
		return -1;
	}

	if(setgid(pw->pw_gid) != 0) {
		error("Cannot switch to user's %s group (gid: %d). Error: %s\n", username, pw->pw_gid, strerror(errno));
		return -1;
	}
	if(setegid(pw->pw_gid) != 0) {
		error("Cannot effectively switch to user's %s group (gid: %d). Error: %s\n", username, pw->pw_gid, strerror(errno));
		return -1;
	}
	if(setuid(pw->pw_uid) != 0) {
		error("Cannot switch to user %s (uid: %d). Error: %s\n", username, pw->pw_uid, strerror(errno));
		return -1;
	}
	if(seteuid(pw->pw_uid) != 0) {
		error("Cannot effectively switch to user %s (uid: %d). Error: %s\n", username, pw->pw_uid, strerror(errno));
		return -1;
	}

	return(0);
}

int become_daemon(int dont_fork, int close_all_files, const char *user, const char *input, const char *output, const char *error, const char *access, int *access_fd, FILE **access_fp)
{
	fflush(NULL);

	// open the files before forking
	int input_fd = -1, output_fd = -1, error_fd = -1, dev_null = -1;

	if(input && *input) {
		if((input_fd = open(input, O_RDONLY, 0666)) == -1) {
			fprintf(stderr, "Cannot open input file '%s' (%s).\n", input, strerror(errno));
			return -1;
		}
	}

	if(output && *output) {
		if((output_fd = open(output, O_RDWR | O_APPEND | O_CREAT, 0666)) == -1) {
			fprintf(stderr, "Cannot open output log file '%s' (%s).\n", output, strerror(errno));
			if(input_fd != -1) close(input_fd);
			return -1;
		}
	}

	if(error && *error) {
		if((error_fd = open(error, O_RDWR | O_APPEND | O_CREAT, 0666)) == -1) {
			fprintf(stderr, "Cannot open error log file '%s' (%s).\n", error, strerror(errno));
			if(input_fd != -1) close(input_fd);
			if(output_fd != -1) close(output_fd);
			return -1;
		}
	}

	if(access && *access && access_fd) {
		if((*access_fd = open(access, O_RDWR | O_APPEND | O_CREAT, 0666)) == -1) {
			fprintf(stderr, "Cannot open access log file '%s' (%s).\n", access, strerror(errno));
			if(input_fd != -1) close(input_fd);
			if(output_fd != -1) close(output_fd);
			if(error_fd != -1) close(error_fd);
			return -1;
		}

		if(access_fp) {
			*access_fp = fdopen(*access_fd, "w");
			if(!*access_fp) {
				fprintf(stderr, "Cannot migrate file's '%s' fd %d (%s).\n", access, *access_fd, strerror(errno));
				if(input_fd != -1) close(input_fd);
				if(output_fd != -1) close(output_fd);
				if(error_fd != -1) close(error_fd);
				close(*access_fd);
				*access_fd = -1;
				return -1;
			}
		}
	}

	if((dev_null = open("/dev/null", O_RDWR, 0666)) == -1) {
		perror("Cannot open /dev/null");
		if(input_fd != -1) close(input_fd);
		if(output_fd != -1) close(output_fd);
		if(error_fd != -1) close(error_fd);
		if(access && access_fd && *access_fd != -1) {
			close(*access_fd);
			*access_fd = -1;
			if(access_fp) {
				fclose(*access_fp);
				*access_fp = NULL;
			}
		}
		return -1;
	}

	// all files opened
	// lets do it

	if(!dont_fork) {
		int i = fork();
		if(i == -1) {
			perror("cannot fork");
			exit(1);
		}
		if(i != 0) {
			exit(0); // the parent
		}

		// become session leader
		if (setsid() < 0) {
			perror("Cannot become session leader.");
			exit(2);
		}
	}

	signal(SIGCHLD,  SIG_IGN);
	signal(SIGHUP,   SIG_IGN);
	signal(SIGWINCH, SIG_IGN);

	// fork() again
	if(!dont_fork) {
		int i = fork();
		if(i == -1) {
			perror("cannot fork");
			exit(1);
		}
		if(i != 0) {
			exit(0); // the parent
		}
	}

	// Set new file permissions
	umask(0);

	// close all files
	if(close_all_files) {
		int i;
		for(i = sysconf(_SC_OPEN_MAX); i > 0; i--)
			if(
				((access_fd && i != *access_fd) || !access_fd)
				&& i != dev_null
				&& i != input_fd
				&& i != output_fd
				&& i != error_fd
				&& fd_is_valid(i)
				) close(i);
	}
	else {
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
	}

	// put the opened files
	// to our standard file descriptors
	if(input_fd != -1) {
		if(input_fd != STDIN_FILENO) {
			dup2(input_fd, STDIN_FILENO);
			close(input_fd);
		}
		input_fd = -1;
	}
	else dup2(dev_null, STDIN_FILENO);

	if(output_fd != -1) {
		if(output_fd != STDOUT_FILENO) {
			dup2(output_fd, STDOUT_FILENO);
			close(output_fd);
		}
		output_fd = -1;
	}
	else dup2(dev_null, STDOUT_FILENO);

	if(error_fd != -1) {
		if(error_fd != STDERR_FILENO) {
			dup2(error_fd, STDERR_FILENO);
			close(error_fd);
		}
		error_fd = -1;
	}
	else dup2(dev_null, STDERR_FILENO);

	// close /dev/null
	if(dev_null != STDIN_FILENO && dev_null != STDOUT_FILENO && dev_null != STDERR_FILENO)
		close(dev_null);

	// generate our pid file
	{
		unlink(pidfile);
		int fd = open(pidfile, O_RDWR | O_CREAT, 0666);
		if(fd >= 0) {
			char b[100];
			sprintf(b, "%d\n", getpid());
			int i = write(fd, b, strlen(b));
			if(i <= 0) perror("Cannot write pid to file.");
			close(fd);
		}
	}

	if(user && *user) {
		if(become_user(user) != 0) {
			error("Cannot become user '%s'. Continuing as we are.", user);
		}
		else info("Successfully became user '%s'.", user);
	}

	return(0);
}
