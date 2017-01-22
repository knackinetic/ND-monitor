#ifndef NETDATA_MAIN_H
#define NETDATA_MAIN_H 1

extern volatile sig_atomic_t netdata_exit;

/**
 * This struct contains information about command line options.
 */
struct option_def {
    /** The option character */
    const char val;
    /** The name of the long option. */
    const char *description;
    /** Short descripton what the option does */
    /** Name of the argument displayed in SYNOPSIS */
    const char *arg_name;
    /** Default value if not set */
    const char *default_value;
};

/**
 * List of command line options.
 * This can be used to compute manpage, help messages, ect.
 */
extern struct option_def options[];

struct netdata_static_thread {
    char *name;

    char *config_section;
    char *config_name;

    volatile int enabled;

    pthread_t *thread;

    void (*init_routine) (void);
    void *(*start_routine) (void *);
};

extern void kill_childs(void);
extern int killpid(pid_t pid, int signal);
extern void netdata_cleanup_and_exit(int ret) NORETURN;

#endif /* NETDATA_MAIN_H */
