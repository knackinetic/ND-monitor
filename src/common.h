#ifndef NETDATA_COMMON_H
#define NETDATA_COMMON_H 1

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

// ----------------------------------------------------------------------------
// system include files for all netdata C programs

/* select the memory allocator, based on autoconf findings */
#if defined(ENABLE_JEMALLOC)

#if defined(HAVE_JEMALLOC_JEMALLOC_H)
#include <jemalloc/jemalloc.h>
#else
#include <malloc.h>
#endif

#elif defined(ENABLE_TCMALLOC)

#include <google/tcmalloc.h>

#else /* !defined(ENABLE_JEMALLOC) && !defined(ENABLE_TCMALLOC) */

#if !(defined(__FreeBSD__) || defined(__APPLE__))
#include <malloc.h>
#endif /* __FreeBSD__ || __APPLE__ */

#endif

#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_RESOLV_H
#include <resolv.h>
#endif

#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <locale.h>

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#include <net/if.h>

#include <poll.h>
#include <signal.h>
#include <syslog.h>
#include <sys/mman.h>

#if !(defined(__FreeBSD__) || defined(__APPLE__))
#include <sys/prctl.h>
#endif /* __FreeBSD__ || __APPLE__*/

#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>

// #1408
#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif

/*
#include <mntent.h>
*/

#ifdef STORAGE_WITH_MATH
#include <math.h>
#endif

#if defined(HAVE_INTTYPES_H)
#include <inttypes.h>
#elif defined(HAVE_STDINT_H)
#include <stdint.h>
#endif

#ifdef NETDATA_WITH_ZLIB
#include <zlib.h>
#endif

// ----------------------------------------------------------------------------
// netdata common definitions

#if (SIZEOF_VOID_P == 8)
#define ENVIRONMENT64
#elif (SIZEOF_VOID_P == 4)
#define ENVIRONMENT32
#else
#error "Cannot detect if this is a 32 or 64 bit CPU"
#endif

#ifdef __GNUC__
#define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#endif // __GNUC__

#ifdef HAVE_FUNC_ATTRIBUTE_RETURNS_NONNULL
#define NEVERNULL __attribute__((returns_nonnull))
#else
#define NEVERNULL
#endif

#ifdef HAVE_FUNC_ATTRIBUTE_MALLOC
#define MALLOCLIKE __attribute__((malloc))
#else
#define MALLOCLIKE
#endif

#ifdef HAVE_FUNC_ATTRIBUTE_FORMAT
#define PRINTFLIKE(f, a) __attribute__ ((format(__printf__, f, a)))
#else
#define PRINTFLIKE(f, a)
#endif

#ifdef HAVE_FUNC_ATTRIBUTE_NORETURN
#define NORETURN __attribute__ ((noreturn))
#else
#define NORETURN
#endif

#ifdef HAVE_FUNC_ATTRIBUTE_WARN_UNUSED_RESULT
#define WARNUNUSED __attribute__ ((warn_unused_result))
#else
#define WARNUNUSED
#endif

#ifdef abs
#undef abs
#endif
#define abs(x) ((x < 0)? -x : x)

#define GUID_LEN 36

// ----------------------------------------------------------------------------
// netdata include files

#include "simple_pattern.h"
#include "avl.h"
#include "clocks.h"
#include "log.h"
#include "global_statistics.h"
#include "storage_number.h"
#include "web_buffer.h"
#include "web_buffer_svg.h"
#include "url.h"
#include "popen.h"

#include "procfile.h"
#include "appconfig.h"
#include "dictionary.h"
#include "proc_self_mountinfo.h"
#include "plugin_checks.h"
#include "plugin_idlejitter.h"
#include "plugin_nfacct.h"

#if defined(__FreeBSD__)
#include "plugin_freebsd.h"
#elif defined(__APPLE__)
#include "plugin_macos.h"
#else
#include "plugin_proc.h"
#include "plugin_proc_diskspace.h"
#endif /* __FreeBSD__, __APPLE__*/

#include "plugin_tc.h"
#include "plugins_d.h"
#include "socket.h"
#include "eval.h"
#include "health.h"
#include "rrd.h"
#include "rrd2json.h"
#include "web_client.h"
#include "web_server.h"
#include "registry.h"
#include "daemon.h"
#include "main.h"
#include "unit_test.h"
#include "ipc.h"
#include "backends.h"
#include "inlined.h"
#include "adaptive_resortable_list.h"

extern void netdata_fix_chart_id(char *s);
extern void netdata_fix_chart_name(char *s);

extern void strreverse(char* begin, char* end);
extern char *mystrsep(char **ptr, char *s);
extern char *trim(char *s);

extern char *strncpyz(char *dst, const char *src, size_t n);
extern int  vsnprintfz(char *dst, size_t n, const char *fmt, va_list args);
extern int  snprintfz(char *dst, size_t n, const char *fmt, ...) PRINTFLIKE(3, 4);

// memory allocation functions that handle failures
#ifdef NETDATA_LOG_ALLOCATIONS
#define strdupz(s) strdupz_int(__FILE__, __FUNCTION__, __LINE__, s)
#define callocz(nmemb, size) callocz_int(__FILE__, __FUNCTION__, __LINE__, nmemb, size)
#define mallocz(size) mallocz_int(__FILE__, __FUNCTION__, __LINE__, size)
#define reallocz(ptr, size) reallocz_int(__FILE__, __FUNCTION__, __LINE__, ptr, size)
#define freez(ptr) freez_int(__FILE__, __FUNCTION__, __LINE__, ptr)

extern char *strdupz_int(const char *file, const char *function, const unsigned long line, const char *s);
extern void *callocz_int(const char *file, const char *function, const unsigned long line, size_t nmemb, size_t size);
extern void *mallocz_int(const char *file, const char *function, const unsigned long line, size_t size);
extern void *reallocz_int(const char *file, const char *function, const unsigned long line, void *ptr, size_t size);
extern void freez_int(const char *file, const char *function, const unsigned long line, void *ptr);
#else
extern char *strdupz(const char *s) MALLOCLIKE NEVERNULL;
extern void *callocz(size_t nmemb, size_t size) MALLOCLIKE NEVERNULL;
extern void *mallocz(size_t size) MALLOCLIKE NEVERNULL;
extern void *reallocz(void *ptr, size_t size) MALLOCLIKE NEVERNULL;
extern void freez(void *ptr);
#endif

extern void json_escape_string(char *dst, const char *src, size_t size);

extern void *mymmap(const char *filename, size_t size, int flags, int ksm);
extern int savememory(const char *filename, void *mem, size_t size);

extern int fd_is_valid(int fd);

extern char *global_host_prefix;
extern int enable_ksm;

extern pid_t gettid(void);

extern int sleep_usec(usec_t usec);

extern char *fgets_trim_len(char *buf, size_t buf_size, FILE *fp, size_t *len);

extern int processors;
extern long get_system_cpus(void);

extern pid_t pid_max;
extern pid_t get_system_pid_max(void);

/* Number of ticks per second */
extern unsigned int hz;
extern void get_system_HZ(void);


/* fix for alpine linux */
#ifndef RUSAGE_THREAD
#ifdef RUSAGE_CHILDREN
#define RUSAGE_THREAD RUSAGE_CHILDREN
#endif
#endif

#endif /* NETDATA_COMMON_H */
