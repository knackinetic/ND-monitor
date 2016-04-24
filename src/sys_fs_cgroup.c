#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

#include "common.h"
#include "appconfig.h"
#include "procfile.h"
#include "log.h"
#include "rrd.h"

// ----------------------------------------------------------------------------
// cgroup globals

static int cgroup_enable_cpuacct_stat = CONFIG_ONDEMAND_ONDEMAND;
static int cgroup_enable_cpuacct_usage = CONFIG_ONDEMAND_ONDEMAND;
static int cgroup_enable_memory = CONFIG_ONDEMAND_ONDEMAND;
static int cgroup_enable_blkio = CONFIG_ONDEMAND_ONDEMAND;
static int cgroup_enable_new_cgroups_detected_at_runtime = 1;
static int cgroup_check_for_new_every = 10;
static char *cgroup_cpuacct_base = NULL;
static char *cgroup_blkio_base = NULL;
static char *cgroup_memory_base = NULL;

static int cgroup_root_count = 0;
static int cgroup_root_max = 50;
static int cgroup_max_depth = 0;

void read_cgroup_plugin_configuration() {
	cgroup_check_for_new_every = config_get_number("plugin:cgroups", "check for new plugin every", cgroup_check_for_new_every);

	cgroup_enable_cpuacct_stat = config_get_boolean_ondemand("plugin:cgroups", "enable cpuacct stat", cgroup_enable_cpuacct_stat);
	cgroup_enable_cpuacct_usage = config_get_boolean_ondemand("plugin:cgroups", "enable cpuacct usage", cgroup_enable_cpuacct_usage);
	cgroup_enable_memory = config_get_boolean_ondemand("plugin:cgroups", "enable memory", cgroup_enable_memory);
	cgroup_enable_blkio = config_get_boolean_ondemand("plugin:cgroups", "enable blkio", cgroup_enable_blkio);

	char filename[FILENAME_MAX + 1];
	snprintf(filename, FILENAME_MAX, "%s%s", global_host_prefix, "/sys/fs/cgroup/cpuacct");
	cgroup_cpuacct_base = config_get("plugin:cgroups", "path to /sys/fs/cgroup/cpuacct", filename);

	snprintf(filename, FILENAME_MAX, "%s%s", global_host_prefix, "/sys/fs/cgroup/blkio");
	cgroup_blkio_base = config_get("plugin:cgroups", "path to /sys/fs/cgroup/blkio", filename);

	snprintf(filename, FILENAME_MAX, "%s%s", global_host_prefix, "/sys/fs/cgroup/memory");
	cgroup_memory_base = config_get("plugin:cgroups", "path to /sys/fs/cgroup/memory", filename);

	cgroup_root_max = config_get_number("plugin:cgroups", "max cgroups to allow", cgroup_root_max);
	cgroup_max_depth = config_get_number("plugin:cgroups", "max cgroups depth to monitor", cgroup_max_depth);

	cgroup_enable_new_cgroups_detected_at_runtime = config_get_boolean("plugin:cgroups", "enable new cgroups detected at run time", cgroup_enable_new_cgroups_detected_at_runtime);
}

// ----------------------------------------------------------------------------
// cgroup objects

struct blkio {
	int updated;

	char *filename;

	unsigned long long Read;
	unsigned long long Write;
	unsigned long long Sync;
	unsigned long long Async;
	unsigned long long Total;
};

// https://www.kernel.org/doc/Documentation/cgroup-v1/memory.txt
struct memory {
	int updated;

	char *filename;

	unsigned long long cache;
	unsigned long long rss;
	unsigned long long rss_huge;
	unsigned long long mapped_file;
	unsigned long long writeback;
	unsigned long long pgpgin;
	unsigned long long pgpgout;
	unsigned long long pgfault;
	unsigned long long pgmajfault;
/*	unsigned long long inactive_anon;
	unsigned long long active_anon;
	unsigned long long inactive_file;
	unsigned long long active_file;
	unsigned long long unevictable;
	unsigned long long hierarchical_memory_limit;
	unsigned long long total_cache;
	unsigned long long total_rss;
	unsigned long long total_rss_huge;
	unsigned long long total_mapped_file;
	unsigned long long total_writeback;
	unsigned long long total_pgpgin;
	unsigned long long total_pgpgout;
	unsigned long long total_pgfault;
	unsigned long long total_pgmajfault;
	unsigned long long total_inactive_anon;
	unsigned long long total_active_anon;
	unsigned long long total_inactive_file;
	unsigned long long total_active_file;
	unsigned long long total_unevictable;
*/
};

// https://www.kernel.org/doc/Documentation/cgroup-v1/cpuacct.txt
struct cpuacct_stat {
	int updated;

	char *filename;

	unsigned long long user;
	unsigned long long system;
};

// https://www.kernel.org/doc/Documentation/cgroup-v1/cpuacct.txt
struct cpuacct_usage {
	int updated;

	char *filename;

	unsigned int cpus;
	unsigned long long *cpu_percpu;
};

struct cgroup {
	int available;
	int enabled;

	char *id;
	uint32_t hash;

	char *name;

	struct cpuacct_stat cpuacct_stat;
	struct cpuacct_usage cpuacct_usage;

	struct memory memory;

	struct blkio io_service_bytes;				// bytes
	struct blkio io_serviced;					// operations

	struct blkio throttle_io_service_bytes;		// bytes
	struct blkio throttle_io_serviced;			// operations

	struct blkio io_merged;						// operations
	struct blkio io_queued;						// operations

	struct cgroup *next;

} *cgroup_root = NULL;

// ----------------------------------------------------------------------------
// read values from /sys

void cgroup_read_cpuacct_stat(struct cpuacct_stat *cp) {
	static procfile *ff = NULL;

	static uint32_t user_hash = 0;
	static uint32_t system_hash = 0;

	if(unlikely(user_hash == 0)) {
		user_hash = simple_hash("user");
		system_hash = simple_hash("system");
	}

	cp->updated = 0;
	if(cp->filename) {
		ff = procfile_reopen(ff, cp->filename, NULL, PROCFILE_FLAG_DEFAULT);
		if(!ff) return;

		ff = procfile_readall(ff);
		if(!ff) return;

		unsigned long i, lines = procfile_lines(ff);

		if(lines < 1) {
			error("File '%s' should have 1+ lines.", cp->filename);
			return;
		}

		for(i = 0; i < lines ; i++) {
			char *s = procfile_lineword(ff, i, 0);
			uint32_t hash = simple_hash(s);

			if(hash == user_hash && !strcmp(s, "user"))
				cp->user = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == system_hash && !strcmp(s, "system"))
				cp->system = strtoull(procfile_lineword(ff, i, 1), NULL, 10);
		}

		cp->updated = 1;

		// fprintf(stderr, "READ '%s': user: %llu, system: %llu\n", cp->filename, cp->user, cp->system);
	}
}

void cgroup_read_cpuacct_usage(struct cpuacct_usage *ca) {
	static procfile *ff = NULL;

	ca->updated = 0;
	if(ca->filename) {
		ff = procfile_reopen(ff, ca->filename, NULL, PROCFILE_FLAG_DEFAULT);
		if(!ff) return;

		ff = procfile_readall(ff);
		if(!ff) return;

		if(procfile_lines(ff) < 1) {
			error("File '%s' should have 1+ lines but has %d.", ca->filename, procfile_lines(ff));
			return;
		}

		unsigned long i = procfile_linewords(ff, 0);
		if(i <= 0) return;

		// we may have 1 more CPU reported
		while(i > 0) {
			char *s = procfile_lineword(ff, 0, i - 1);
			if(!*s) i--;
			else break;
		}

		if(i != ca->cpus) {
			free(ca->cpu_percpu);
		}
		ca->cpu_percpu = malloc(sizeof(unsigned long long) * i);
		if(!ca->cpu_percpu)
			fatal("Cannot allocate memory (%z bytes)", sizeof(unsigned long long) * i);

		ca->cpus = i;

		for(i = 0; i < ca->cpus ;i++) {
			ca->cpu_percpu[i] = strtoull(procfile_lineword(ff, 0, i), NULL, 10);
			// fprintf(stderr, "READ '%s': cpu%d/%d: %llu ('%s')\n", ca->filename, i, ca->cpus, ca->cpu_percpu[i], procfile_lineword(ff, 0, i));
		}

		ca->updated = 1;
	}
}

void cgroup_read_blkio(struct blkio *io) {
	static procfile *ff = NULL;

	static uint32_t Read_hash = 0;
	static uint32_t Write_hash = 0;
	static uint32_t Sync_hash = 0;
	static uint32_t Async_hash = 0;
	static uint32_t Total_hash = 0;

	if(unlikely(Read_hash == 0)) {
		Read_hash = simple_hash("Read");
		Write_hash = simple_hash("Write");
		Sync_hash = simple_hash("Sync");
		Async_hash = simple_hash("Async");
		Total_hash = simple_hash("Total");
	}

	io->updated = 0;
	if(io->filename) {
		ff = procfile_reopen(ff, io->filename, NULL, PROCFILE_FLAG_DEFAULT);
		if(!ff) return;

		ff = procfile_readall(ff);
		if(!ff) return;

		unsigned long i, lines = procfile_lines(ff);

		if(lines < 1) {
			error("File '%s' should have 1+ lines.", io->filename);
			return;
		}

		io->Read = 0;
		io->Write = 0;
		io->Sync = 0;
		io->Async = 0;
		io->Total = 0;

		for(i = 0; i < lines ; i++) {
			char *s = procfile_lineword(ff, i, 1);
			uint32_t hash = simple_hash(s);

			if(hash == Read_hash && !strcmp(s, "Read"))
				io->Read += strtoull(procfile_lineword(ff, i, 2), NULL, 10);

			else if(hash == Write_hash && !strcmp(s, "Write"))
				io->Write += strtoull(procfile_lineword(ff, i, 2), NULL, 10);

			else if(hash == Sync_hash && !strcmp(s, "Sync"))
				io->Sync += strtoull(procfile_lineword(ff, i, 2), NULL, 10);

			else if(hash == Async_hash && !strcmp(s, "Async"))
				io->Async += strtoull(procfile_lineword(ff, i, 2), NULL, 10);

			else if(hash == Total_hash && !strcmp(s, "Total"))
				io->Total += strtoull(procfile_lineword(ff, i, 2), NULL, 10);
		}

		io->updated = 1;
		// fprintf(stderr, "READ '%s': Read: %llu, Write: %llu, Sync: %llu, Async: %llu, Total: %llu\n", io->filename, io->Read, io->Write, io->Sync, io->Async, io->Total);
	}
}

void cgroup_read_memory(struct memory *mem) {
	static procfile *ff = NULL;

	static uint32_t cache_hash = 0;
	static uint32_t rss_hash = 0;
	static uint32_t rss_huge_hash = 0;
	static uint32_t mapped_file_hash = 0;
	static uint32_t writeback_hash = 0;
	static uint32_t pgpgin_hash = 0;
	static uint32_t pgpgout_hash = 0;
	static uint32_t pgfault_hash = 0;
	static uint32_t pgmajfault_hash = 0;
/*	static uint32_t inactive_anon_hash = 0;
	static uint32_t active_anon_hash = 0;
	static uint32_t inactive_file_hash = 0;
	static uint32_t active_file_hash = 0;
	static uint32_t unevictable_hash = 0;
	static uint32_t hierarchical_memory_limit_hash = 0;
	static uint32_t total_cache_hash = 0;
	static uint32_t total_rss_hash = 0;
	static uint32_t total_rss_huge_hash = 0;
	static uint32_t total_mapped_file_hash = 0;
	static uint32_t total_writeback_hash = 0;
	static uint32_t total_pgpgin_hash = 0;
	static uint32_t total_pgpgout_hash = 0;
	static uint32_t total_pgfault_hash = 0;
	static uint32_t total_pgmajfault_hash = 0;
	static uint32_t total_inactive_anon_hash = 0;
	static uint32_t total_active_anon_hash = 0;
	static uint32_t total_inactive_file_hash = 0;
	static uint32_t total_active_file_hash = 0;
	static uint32_t total_unevictable_hash = 0;
*/
	if(unlikely(cache_hash == 0)) {
		cache_hash = simple_hash("cache");
		rss_hash = simple_hash("rss");
		rss_huge_hash = simple_hash("rss_huge");
		mapped_file_hash = simple_hash("mapped_file");
		writeback_hash = simple_hash("writeback");
		pgpgin_hash = simple_hash("pgpgin");
		pgpgout_hash = simple_hash("pgpgout");
		pgfault_hash = simple_hash("pgfault");
		pgmajfault_hash = simple_hash("pgmajfault");
/*		inactive_anon_hash = simple_hash("inactive_anon");
		active_anon_hash = simple_hash("active_anon");
		inactive_file_hash = simple_hash("inactive_file");
		active_file_hash = simple_hash("active_file");
		unevictable_hash = simple_hash("unevictable");
		hierarchical_memory_limit_hash = simple_hash("hierarchical_memory_limit");
		total_cache_hash = simple_hash("total_cache");
		total_rss_hash = simple_hash("total_rss");
		total_rss_huge_hash = simple_hash("total_rss_huge");
		total_mapped_file_hash = simple_hash("total_mapped_file");
		total_writeback_hash = simple_hash("total_writeback");
		total_pgpgin_hash = simple_hash("total_pgpgin");
		total_pgpgout_hash = simple_hash("total_pgpgout");
		total_pgfault_hash = simple_hash("total_pgfault");
		total_pgmajfault_hash = simple_hash("total_pgmajfault");
		total_inactive_anon_hash = simple_hash("total_inactive_anon");
		total_active_anon_hash = simple_hash("total_active_anon");
		total_inactive_file_hash = simple_hash("total_inactive_file");
		total_active_file_hash = simple_hash("total_active_file");
		total_unevictable_hash = simple_hash("total_unevictable");
*/
	}

	mem->updated = 0;
	if(mem->filename) {
		ff = procfile_reopen(ff, mem->filename, NULL, PROCFILE_FLAG_DEFAULT);
		if(!ff) return;

		ff = procfile_readall(ff);
		if(!ff) return;

		unsigned long i, lines = procfile_lines(ff);

		if(lines < 1) {
			error("File '%s' should have 1+ lines.", mem->filename);
			return;
		}

		for(i = 0; i < lines ; i++) {
			char *s = procfile_lineword(ff, i, 0);
			uint32_t hash = simple_hash(s);

			if(hash == cache_hash && !strcmp(s, "cache"))
				mem->cache = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == rss_hash && !strcmp(s, "rss"))
				mem->rss = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == rss_huge_hash && !strcmp(s, "rss_huge"))
				mem->rss_huge = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == mapped_file_hash && !strcmp(s, "mapped_file"))
				mem->mapped_file = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == writeback_hash && !strcmp(s, "writeback"))
				mem->writeback = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == pgpgin_hash && !strcmp(s, "pgpgin"))
				mem->pgpgin = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == pgpgout_hash && !strcmp(s, "pgpgout"))
				mem->pgpgout = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == pgfault_hash && !strcmp(s, "pgfault"))
				mem->pgfault = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == pgmajfault_hash && !strcmp(s, "pgmajfault"))
				mem->pgmajfault = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

/*			else if(hash == inactive_anon_hash && !strcmp(s, "inactive_anon"))
				mem->inactive_anon = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == active_anon_hash && !strcmp(s, "active_anon"))
				mem->active_anon = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == inactive_file_hash && !strcmp(s, "inactive_file"))
				mem->inactive_file = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == active_file_hash && !strcmp(s, "active_file"))
				mem->active_file = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == unevictable_hash && !strcmp(s, "unevictable"))
				mem->unevictable = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == hierarchical_memory_limit_hash && !strcmp(s, "hierarchical_memory_limit"))
				mem->hierarchical_memory_limit = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

	    	else if(hash == total_cache_hash && !strcmp(s, "total_cache"))
				mem->total_cache = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == total_rss_hash && !strcmp(s, "total_rss"))
				mem->total_rss = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == total_rss_huge_hash && !strcmp(s, "total_rss_huge"))
				mem->total_rss_huge = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == total_mapped_file_hash && !strcmp(s, "total_mapped_file"))
				mem->total_mapped_file = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == total_writeback_hash && !strcmp(s, "total_writeback"))
				mem->total_writeback = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == total_pgpgin_hash && !strcmp(s, "total_pgpgin"))
				mem->total_pgpgin = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == total_pgpgout_hash && !strcmp(s, "total_pgpgout"))
				mem->total_pgpgout = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == total_pgfault_hash && !strcmp(s, "total_pgfault"))
				mem->total_pgfault = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == total_pgmajfault_hash && !strcmp(s, "total_pgmajfault"))
				mem->total_pgmajfault = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == total_inactive_anon_hash && !strcmp(s, "total_inactive_anon"))
				mem->total_inactive_anon = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == total_active_anon_hash && !strcmp(s, "total_active_anon"))
				mem->total_active_anon = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == total_inactive_file_hash && !strcmp(s, "total_inactive_file"))
				mem->total_inactive_file = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == total_active_file_hash && !strcmp(s, "total_active_file"))
				mem->total_active_file = strtoull(procfile_lineword(ff, i, 1), NULL, 10);

			else if(hash == total_unevictable_hash && !strcmp(s, "total_unevictable"))
				mem->total_unevictable = strtoull(procfile_lineword(ff, i, 1), NULL, 10);
*/
		}

		// fprintf(stderr, "READ: '%s', cache: %llu, rss: %llu, rss_huge: %llu, mapped_file: %llu, writeback: %llu, pgpgin: %llu, pgpgout: %llu, pgfault: %llu, pgmajfault: %llu, inactive_anon: %llu, active_anon: %llu, inactive_file: %llu, active_file: %llu, unevictable: %llu, hierarchical_memory_limit: %llu, total_cache: %llu, total_rss: %llu, total_rss_huge: %llu, total_mapped_file: %llu, total_writeback: %llu, total_pgpgin: %llu, total_pgpgout: %llu, total_pgfault: %llu, total_pgmajfault: %llu, total_inactive_anon: %llu, total_active_anon: %llu, total_inactive_file: %llu, total_active_file: %llu, total_unevictable: %llu\n", mem->filename, mem->cache, mem->rss, mem->rss_huge, mem->mapped_file, mem->writeback, mem->pgpgin, mem->pgpgout, mem->pgfault, mem->pgmajfault, mem->inactive_anon, mem->active_anon, mem->inactive_file, mem->active_file, mem->unevictable, mem->hierarchical_memory_limit, mem->total_cache, mem->total_rss, mem->total_rss_huge, mem->total_mapped_file, mem->total_writeback, mem->total_pgpgin, mem->total_pgpgout, mem->total_pgfault, mem->total_pgmajfault, mem->total_inactive_anon, mem->total_active_anon, mem->total_inactive_file, mem->total_active_file, mem->total_unevictable);

		mem->updated = 1;
	}
}

void cgroup_read(struct cgroup *cg) {
	cgroup_read_cpuacct_stat(&cg->cpuacct_stat);
	cgroup_read_cpuacct_usage(&cg->cpuacct_usage);
	cgroup_read_memory(&cg->memory);
	cgroup_read_blkio(&cg->io_service_bytes);
	cgroup_read_blkio(&cg->io_serviced);
	cgroup_read_blkio(&cg->throttle_io_service_bytes);
	cgroup_read_blkio(&cg->throttle_io_serviced);
	cgroup_read_blkio(&cg->io_merged);
	cgroup_read_blkio(&cg->io_queued);
}

void read_all_cgroups(struct cgroup *cg) {
	struct cgroup *i;

	for(i = cg; i ; i = i->next)
		cgroup_read(i);
}

// ----------------------------------------------------------------------------
// add/remove/find cgroup objects

struct cgroup *cgroup_add(const char *id) {
	if(cgroup_root_count >= cgroup_root_max) {
		info("Maximum number of cgroups reached (%d). Not adding cgroup '%s'", cgroup_root_count, id);
		return NULL;
	}

	int def = cgroup_enable_new_cgroups_detected_at_runtime;
	const char *name = id;
	if(!*name) {
		name = "/";

		// disable by default the host cgroup
		def = 0;
	}
	else {
		if(*name == '/') name++;

		// disable by default the parent cgroup
		// for known cgroup managers
		if(!strcmp(name, "lxc") || !strcmp(name, "docker"))
			def = 0;
	}

	char option[FILENAME_MAX + 1];
	snprintf(option, FILENAME_MAX, "enable cgroup %s", name);
	if(!config_get_boolean("plugin:cgroups", option, def))
		return NULL;

	struct cgroup *cg = calloc(1, sizeof(struct cgroup));
	if(cg) {
		cg->id = strdup(id);
		if(!cg->id) fatal("Cannot allocate memory for cgroup '%s'", id);

		cg->hash = simple_hash(cg->id);

		cg->name = strdup(name);
		if(!cg->name) fatal("Cannot allocate memory for cgroup '%s'", id);

		if(!cgroup_root)
			cgroup_root = cg;
		else {
			// append it
			struct cgroup *e;
			for(e = cgroup_root; e->next ;e = e->next) ;
			e->next = cg;
		}

		cgroup_root_count++;

		// fprintf(stderr, " > added cgroup No %d, with id '%s' (%u) and name '%s'\n", cgroup_root_count, cg->id, cg->hash, cg->name);
	}
	else fatal("Cannot allocate memory for cgroup '%s'", id);

	return cg;
}

void cgroup_remove(struct cgroup *cg) {
	if(cg == cgroup_root) {
		cgroup_root = cg->next;
	}
	else {
		struct cgroup *e;
		for(e = cgroup_root; e->next ;e = e->next)
			if(unlikely(e->next == cg)) break;

		if(e->next != cg) {
			error("Cannot find cgroup '%s' in list of cgroups", cg->id);
		}
		else {
			e->next = cg->next;
			cg->next = NULL;
		}
	}

	free(cg->cpuacct_usage.cpu_percpu);

	free(cg->cpuacct_stat.filename);
	free(cg->cpuacct_usage.filename);
	free(cg->memory.filename);
	free(cg->io_service_bytes.filename);
	free(cg->io_serviced.filename);
	free(cg->throttle_io_service_bytes.filename);
	free(cg->throttle_io_serviced.filename);
	free(cg->io_merged.filename);
	free(cg->io_queued.filename);

	free(cg->id);
	free(cg->name);
	free(cg);
}

// find if a given cgroup exists
struct cgroup *cgroup_find(const char *id) {
	uint32_t hash = simple_hash(id);

	// fprintf(stderr, " > searching for '%s' (%u)\n", id, hash);

	struct cgroup *cg;
	for(cg = cgroup_root; cg ; cg = cg->next) {
		if(hash == cg->hash && strcmp(id, cg->id) == 0)
			break;
	}

	return cg;
}

// ----------------------------------------------------------------------------
// detect running cgroups

// callback for find_file_in_subdirs()
void found_dir_in_subdir(const char *dir) {
	// fprintf(stderr, "found dir '%s'\n", dir);

	struct cgroup *cg = cgroup_find(dir);
	if(!cg) {
		if(*dir && cgroup_max_depth > 0) {
			int depth = 0;
			const char *s;

			for(s = dir; *s ;s++)
				if(unlikely(*s == '/'))
					depth++;

			if(depth > cgroup_max_depth) {
				info("cgroup '%s' is too deep (%d, while max is %d)", dir, depth, cgroup_max_depth);
				return;
			}
		}
		cg = cgroup_add(dir);
	}

	if(cg) cg->available = 1;
}

void find_dir_in_subdirs(const char *base, const char *this, void (*callback)(const char *)) {
	if(!this) this = base;
	size_t dirlen = strlen(this), baselen = strlen(base);

	DIR *dir = opendir(this);
	if(!dir) return;

	callback(&this[baselen]);

	struct dirent *de = NULL;
	while((de = readdir(dir))) {
		if(de->d_type == DT_DIR
			&& (
				(de->d_name[0] == '.' && de->d_name[1] == '\0')
				|| (de->d_name[0] == '.' && de->d_name[1] == '.' && de->d_name[2] == '\0')
				))
			continue;

		// fprintf(stderr, "examining '%s/%s'\n", this, de->d_name);

		if(de->d_type == DT_DIR) {
			char *s = malloc(dirlen + strlen(de->d_name) + 2);
			if(s) {
				strcpy(s, this);
				strcat(s, "/");
				strcat(s, de->d_name);
				find_dir_in_subdirs(base, s, callback);
				free(s);
			}
		}
	}

	closedir(dir);
}

void mark_all_cgroups_as_not_available() {
	struct cgroup *cg;

	// mark all as not available
	for(cg = cgroup_root; cg ; cg = cg->next)
		cg->available = 0;
}

struct cgroup *find_all_cgroups() {

	mark_all_cgroups_as_not_available();

	if(cgroup_enable_cpuacct_stat || cgroup_enable_cpuacct_usage)
		find_dir_in_subdirs(cgroup_cpuacct_base, NULL, found_dir_in_subdir);

	if(cgroup_enable_blkio)
		find_dir_in_subdirs(cgroup_blkio_base, NULL, found_dir_in_subdir);

	if(cgroup_enable_memory)
		find_dir_in_subdirs(cgroup_memory_base, NULL, found_dir_in_subdir);

	struct cgroup *cg;
	for(cg = cgroup_root; cg ; cg = cg->next) {
		// fprintf(stderr, " >>> CGROUP '%s' (%u - %s) with name '%s'\n", cg->id, cg->hash, cg->available?"available":"stopped", cg->name);

		if(!cg->available) {
			cgroup_remove(cg);
			continue;
		}

		// check for newly added cgroups
		// and update the filenames they read
		char filename[FILENAME_MAX + 1];
		if(cgroup_enable_cpuacct_stat && !cg->cpuacct_stat.filename) {
			snprintf(filename, FILENAME_MAX, "%s%s/cpuacct.stat", cgroup_cpuacct_base, cg->id);
			cg->cpuacct_stat.filename = strdup(filename);
		}
		if(cgroup_enable_cpuacct_usage && !cg->cpuacct_usage.filename) {
			snprintf(filename, FILENAME_MAX, "%s%s/cpuacct.usage_percpu", cgroup_cpuacct_base, cg->id);
			cg->cpuacct_usage.filename = strdup(filename);
		}
		if(cgroup_enable_memory && !cg->memory.filename) {
			snprintf(filename, FILENAME_MAX, "%s%s/memory.stat", cgroup_memory_base, cg->id);
			cg->memory.filename = strdup(filename);
		}
		if(cgroup_enable_blkio) {
			if(!cg->io_service_bytes.filename) {
				snprintf(filename, FILENAME_MAX, "%s%s/blkio.io_service_bytes", cgroup_blkio_base, cg->id);
				cg->io_service_bytes.filename = strdup(filename);
			}
			if(!cg->io_serviced.filename) {
				snprintf(filename, FILENAME_MAX, "%s%s/blkio.io_serviced", cgroup_blkio_base, cg->id);
				cg->io_serviced.filename = strdup(filename);
			}
			if(!cg->throttle_io_service_bytes.filename) {
				snprintf(filename, FILENAME_MAX, "%s%s/blkio.throttle.io_service_bytes", cgroup_blkio_base, cg->id);
				cg->throttle_io_service_bytes.filename = strdup(filename);
			}
			if(!cg->throttle_io_serviced.filename) {
				snprintf(filename, FILENAME_MAX, "%s%s/blkio.throttle.io_serviced", cgroup_blkio_base, cg->id);
				cg->throttle_io_serviced.filename = strdup(filename);
			}
			if(!cg->io_merged.filename) {
				snprintf(filename, FILENAME_MAX, "%s%s/blkio.io_merged", cgroup_blkio_base, cg->id);
				cg->io_merged.filename = strdup(filename);
			}
			if(!cg->io_queued.filename) {
				snprintf(filename, FILENAME_MAX, "%s%s/blkio.io_queued", cgroup_blkio_base, cg->id);
				cg->io_queued.filename = strdup(filename);
			}
		}
	}

	return cgroup_root;
}

// ----------------------------------------------------------------------------
// generate charts

#define CHART_TITLE_MAX 300

void update_cgroup_charts(int update_every) {
	char type[RRD_ID_LENGTH_MAX + 1];
	char title[CHART_TITLE_MAX + 1];

	struct cgroup *cg;
	RRDSET *st;

	for(cg = cgroup_root; cg ; cg = cg->next) {
		if(!cg->available) continue;

		if(cg->id[0] == '\0')
			strcpy(type, "cgroup_host");
		else if(cg->id[0] == '/')
			snprintf(type, RRD_ID_LENGTH_MAX, "cgroup%s", cg->id);
		else
			snprintf(type, RRD_ID_LENGTH_MAX, "cgroup_%s", cg->id);

		netdata_fix_chart_id(type);

		if(cg->cpuacct_stat.updated) {
			st = rrdset_find_bytype(type, "cpu");
			if(!st) {
				snprintf(title, CHART_TITLE_MAX, "CPU Usage for cgroup %s", cg->name);
				st = rrdset_create(type, "cpu", NULL, "cpu", "cgroup.cpu", title, "%", 40000, update_every, RRDSET_TYPE_STACKED);

				rrddim_add(st, "user", NULL, 100, hz, RRDDIM_INCREMENTAL);
				rrddim_add(st, "system", NULL, 100, hz, RRDDIM_INCREMENTAL);
			}
			else rrdset_next(st);

			rrddim_set(st, "user", cg->cpuacct_stat.user);
			rrddim_set(st, "system", cg->cpuacct_stat.system);
			rrdset_done(st);
		}

		if(cg->cpuacct_usage.updated) {
			char id[RRD_ID_LENGTH_MAX + 1];
			unsigned int i;

			st = rrdset_find_bytype(type, "cpu_per_core");
			if(!st) {
				snprintf(title, CHART_TITLE_MAX, "CPU Usage Per Core for cgroup %s", cg->name);
				st = rrdset_create(type, "cpu_per_core", NULL, "cpu", "cgroup.cpu_per_core", title, "%", 40100, update_every, RRDSET_TYPE_STACKED);

				for(i = 0; i < cg->cpuacct_usage.cpus ;i++) {
					snprintf(id, CHART_TITLE_MAX, "cpu%d", i);
					rrddim_add(st, id, NULL, 100, 1000000, RRDDIM_INCREMENTAL);
				}
			}
			else rrdset_next(st);

			for(i = 0; i < cg->cpuacct_usage.cpus ;i++) {
				snprintf(id, CHART_TITLE_MAX, "cpu%d", i);
				rrddim_set(st, id, cg->cpuacct_usage.cpu_percpu[i]);
			}
			rrdset_done(st);
		}

		if(cg->memory.updated) {
			if(cg->memory.cache + cg->memory.rss + cg->memory.rss_huge + cg->memory.mapped_file > 0) {
				st = rrdset_find_bytype(type, "mem");
				if(!st) {
					snprintf(title, CHART_TITLE_MAX, "Memory Usage for cgroup %s", cg->name);
					st = rrdset_create(type, "mem", NULL, "mem", "cgroup.mem", title, "MB", 40200, update_every,
					                   RRDSET_TYPE_STACKED);

					rrddim_add(st, "cache", NULL, 1, 1024 * 1024, RRDDIM_ABSOLUTE);
					rrddim_add(st, "rss", NULL, 1, 1024 * 1024, RRDDIM_ABSOLUTE);
					rrddim_add(st, "rss_huge", NULL, 1, 1024 * 1024, RRDDIM_ABSOLUTE);
					rrddim_add(st, "mapped_file", NULL, 1, 1024 * 1024, RRDDIM_ABSOLUTE);
				}
				else rrdset_next(st);

				rrddim_set(st, "cache", cg->memory.cache);
				rrddim_set(st, "rss", cg->memory.rss);
				rrddim_set(st, "rss_huge", cg->memory.rss_huge);
				rrddim_set(st, "mapped_file", cg->memory.mapped_file);
				rrdset_done(st);
			}

			st = rrdset_find_bytype(type, "writeback");
			if(!st) {
				snprintf(title, CHART_TITLE_MAX, "Writeback Memory for cgroup %s", cg->name);
				st = rrdset_create(type, "writeback", NULL, "mem", "cgroup.writeback", title, "MB", 40300,
				                   update_every, RRDSET_TYPE_AREA);

				rrddim_add(st, "writeback", NULL, 1, 1024 * 1024, RRDDIM_ABSOLUTE);
			}
			else rrdset_next(st);

			rrddim_set(st, "writeback", cg->memory.writeback);
			rrdset_done(st);

			if(cg->memory.pgpgin + cg->memory.pgpgout > 0) {
				st = rrdset_find_bytype(type, "mem_activity");
				if(!st) {
					snprintf(title, CHART_TITLE_MAX, "Memory Activity for cgroup %s", cg->name);
					st = rrdset_create(type, "mem_activity", NULL, "mem", "cgroup.mem_activity", title, "MB/s",
					                   40400, update_every, RRDSET_TYPE_LINE);

					rrddim_add(st, "pgpgin", "in", sysconf(_SC_PAGESIZE), 1024 * 1024, RRDDIM_INCREMENTAL);
					rrddim_add(st, "pgpgout", "out", -sysconf(_SC_PAGESIZE), 1024 * 1024, RRDDIM_INCREMENTAL);
				}
				else rrdset_next(st);

				rrddim_set(st, "pgpgin", cg->memory.pgpgin);
				rrddim_set(st, "pgpgout", cg->memory.pgpgout);
				rrdset_done(st);
			}

			if(cg->memory.pgfault + cg->memory.pgmajfault > 0) {
				st = rrdset_find_bytype(type, "pgfaults");
				if(!st) {
					snprintf(title, CHART_TITLE_MAX, "Memory Page Faults for cgroup %s", cg->name);
					st = rrdset_create(type, "pgfaults", NULL, "mem", "cgroup.pgfaults", title, "MB/s", 40500,
					                   update_every, RRDSET_TYPE_LINE);

					rrddim_add(st, "pgfault", NULL, sysconf(_SC_PAGESIZE), 1024 * 1024, RRDDIM_INCREMENTAL);
					rrddim_add(st, "pgmajfault", "swap", -sysconf(_SC_PAGESIZE), 1024 * 1024, RRDDIM_INCREMENTAL);
				}
				else rrdset_next(st);

				rrddim_set(st, "pgfault", cg->memory.pgfault);
				rrddim_set(st, "pgmajfault", cg->memory.pgmajfault);
				rrdset_done(st);
			}
		}

		if(cg->io_service_bytes.updated && cg->io_service_bytes.Read + cg->io_service_bytes.Write > 0) {
			st = rrdset_find_bytype(type, "io");
			if(!st) {
				snprintf(title, CHART_TITLE_MAX, "I/O Bandwidth (all disks) for cgroup %s", cg->name);
				st = rrdset_create(type, "io", NULL, "disk", "cgroup.io", title, "KB/s", 41200,
				                   update_every, RRDSET_TYPE_LINE);

				rrddim_add(st, "read", NULL, 1, 1024, RRDDIM_INCREMENTAL);
				rrddim_add(st, "write", NULL, -1, 1024, RRDDIM_INCREMENTAL);
			}
			else rrdset_next(st);

			rrddim_set(st, "read", cg->io_service_bytes.Read);
			rrddim_set(st, "write", cg->io_service_bytes.Write);
			rrdset_done(st);
		}

		if(cg->io_serviced.updated && cg->io_serviced.Read + cg->io_serviced.Write > 0) {
			st = rrdset_find_bytype(type, "serviced_ops");
			if(!st) {
				snprintf(title, CHART_TITLE_MAX, "Serviced I/O Operations (all disks) for cgroup %s", cg->name);
				st = rrdset_create(type, "serviced_ops", NULL, "disk", "cgroup.serviced_ops", title, "operations/s", 41200,
				                   update_every, RRDSET_TYPE_LINE);

				rrddim_add(st, "read", NULL, 1, 1, RRDDIM_INCREMENTAL);
				rrddim_add(st, "write", NULL, -1, 1, RRDDIM_INCREMENTAL);
			}
			else rrdset_next(st);

			rrddim_set(st, "read", cg->io_serviced.Read);
			rrddim_set(st, "write", cg->io_serviced.Write);
			rrdset_done(st);
		}

		if(cg->throttle_io_service_bytes.updated && cg->throttle_io_service_bytes.Read + cg->throttle_io_service_bytes.Write > 0) {
			st = rrdset_find_bytype(type, "io");
			if(!st) {
				snprintf(title, CHART_TITLE_MAX, "Throttle I/O Bandwidth (all disks) for cgroup %s", cg->name);
				st = rrdset_create(type, "io", NULL, "disk", "cgroup.io", title, "KB/s", 41200,
				                   update_every, RRDSET_TYPE_LINE);

				rrddim_add(st, "read", NULL, 1, 1024, RRDDIM_INCREMENTAL);
				rrddim_add(st, "write", NULL, -1, 1024, RRDDIM_INCREMENTAL);
			}
			else rrdset_next(st);

			rrddim_set(st, "read", cg->throttle_io_service_bytes.Read);
			rrddim_set(st, "write", cg->throttle_io_service_bytes.Write);
			rrdset_done(st);
		}


		if(cg->throttle_io_serviced.updated && cg->throttle_io_serviced.Read + cg->throttle_io_serviced.Write > 0) {
			st = rrdset_find_bytype(type, "throttle_serviced_ops");
			if(!st) {
				snprintf(title, CHART_TITLE_MAX, "Throttle Serviced I/O Operations (all disks) for cgroup %s", cg->name);
				st = rrdset_create(type, "throttle_serviced_ops", NULL, "disk", "cgroup.throttle_serviced_ops", title, "operations/s", 41200,
				                   update_every, RRDSET_TYPE_LINE);

				rrddim_add(st, "read", NULL, 1, 1, RRDDIM_INCREMENTAL);
				rrddim_add(st, "write", NULL, -1, 1, RRDDIM_INCREMENTAL);
			}
			else rrdset_next(st);

			rrddim_set(st, "read", cg->throttle_io_serviced.Read);
			rrddim_set(st, "write", cg->throttle_io_serviced.Write);
			rrdset_done(st);
		}

		if(cg->io_queued.updated) {
			st = rrdset_find_bytype(type, "queued_ops");
			if(!st) {
				snprintf(title, CHART_TITLE_MAX, "Queued I/O Operations (all disks) for cgroup %s", cg->name);
				st = rrdset_create(type, "queued_ops", NULL, "disk", "cgroup.queued_ops", title, "operations", 42000,
				                   update_every, RRDSET_TYPE_LINE);

				rrddim_add(st, "read", NULL, 1, 1, RRDDIM_ABSOLUTE);
				rrddim_add(st, "write", NULL, -1, 1, RRDDIM_ABSOLUTE);
			}
			else rrdset_next(st);

			rrddim_set(st, "read", cg->io_queued.Read);
			rrddim_set(st, "write", cg->io_queued.Write);
			rrdset_done(st);
		}

		if(cg->io_merged.updated && cg->io_merged.Read + cg->io_merged.Write > 0) {
			st = rrdset_find_bytype(type, "merged_ops");
			if(!st) {
				snprintf(title, CHART_TITLE_MAX, "Merged I/O Operations (all disks) for cgroup %s", cg->name);
				st = rrdset_create(type, "merged_ops", NULL, "disk", "cgroup.merged_ops", title, "operations/s", 42100,
				                   update_every, RRDSET_TYPE_LINE);

				rrddim_add(st, "read", NULL, 1, 1024, RRDDIM_INCREMENTAL);
				rrddim_add(st, "write", NULL, -1, 1024, RRDDIM_INCREMENTAL);
			}
			else rrdset_next(st);

			rrddim_set(st, "read", cg->io_merged.Read);
			rrddim_set(st, "write", cg->io_merged.Write);
			rrdset_done(st);
		}
	}
}

// ----------------------------------------------------------------------------
// cgroups main

int do_sys_fs_cgroup(int update_every, unsigned long long dt) {
	static int cgroup_global_config_read = 0;
	static time_t last_run = 0;
	time_t now = time(NULL);

	if(dt) {};

	if(unlikely(!cgroup_global_config_read)) {
		read_cgroup_plugin_configuration();
		cgroup_global_config_read = 1;
	}

	if(unlikely(cgroup_enable_new_cgroups_detected_at_runtime && now - last_run > cgroup_check_for_new_every)) {
		find_all_cgroups();
		last_run = now;
	}

	read_all_cgroups(cgroup_root);
	update_cgroup_charts(update_every);

	return 0;
}
