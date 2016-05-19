#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>


#include "common.h"
#include "log.h"
#include "appconfig.h"
#include "procfile.h"
#include "rrd.h"
#include "plugin_proc.h"

#include "proc_self_mountinfo.h"

#define RRD_TYPE_DISK "disk"

#define DISK_TYPE_PHYSICAL  1
#define DISK_TYPE_PARTITION 2
#define DISK_TYPE_CONTAINER 3

struct disk {
	unsigned long major;
	unsigned long minor;
	int type;
	char *mount_point;
	struct disk *next;
} *disk_root = NULL;

struct disk *get_disk(unsigned long major, unsigned long minor) {
	static char path_find_block_device[FILENAME_MAX + 1] = "";
	static struct mountinfo *mountinfo_root = NULL;
	struct disk *d;

	// search for it in our RAM list.
	// this is sequential, but since we just walk through
	// and the number of disks / partitions in a system
	// should not be that many, it should be acceptable
	for(d = disk_root; d ; d = d->next)
		if(unlikely(d->major == major && d->minor == minor))
			break;

	// if we found it, return it
	if(likely(d))
		return d;

	if(unlikely(!path_find_block_device[0])) {
		char dirname[FILENAME_MAX + 1];
		snprintfz(dirname, FILENAME_MAX, "%s%s", global_host_prefix, "/sys/dev/block/%lu:%lu/%s");
		snprintfz(path_find_block_device, FILENAME_MAX, "%s", config_get("plugin:proc:/proc/diskstats", "path to get block device infos", dirname));
	}

	// not found
	// create a new disk structure
	d = (struct disk *)malloc(sizeof(struct disk));
	if(!d) fatal("Cannot allocate memory for struct disk in proc_diskstats.");

	d->major = major;
	d->minor = minor;
	d->type = DISK_TYPE_PHYSICAL; // Default type. Changed later if not correct.
	d->next = NULL;

	// append it to the list
	if(!disk_root)
		disk_root = d;
	else {
		struct disk *last;
		for(last = disk_root; last->next ;last = last->next);
		last->next = d;
	}

	// ------------------------------------------------------------------------
	// find the type of the device

	// find if it is a partition
	// by checking if /sys/dev/block/MAJOR:MINOR/partition is readable.
	char buffer[FILENAME_MAX + 1];
	snprintfz(buffer, FILENAME_MAX, path_find_block_device, major, minor, "partition");
	if(access(buffer, R_OK) == 0) {
		d->type = DISK_TYPE_PARTITION;
	} else {
		// find if it is a container
		// by checking if /sys/dev/block/MAJOR:MINOR/slaves has entries
		int is_container = 0;
		snprintfz(buffer, FILENAME_MAX, path_find_block_device, major, minor, "slaves/");
		DIR *dirp = opendir(buffer);	
		if (dirp != NULL) {
		struct dirent *dp;
			while(dp = readdir(dirp)) {
				// . and .. are also files in empty folders.
				if(strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
					continue;
				}
				d->type = DISK_TYPE_CONTAINER;
				// Stop the loop after we found one file.
				break;
			}
			if(closedir(dirp) == -1)
				error("Unable to close dir %s", buffer);
		}
	}

	// ------------------------------------------------------------------------
	// check if we can find its mount point

	// mountinfo_find() can be called with NULL mountinfo_root
	struct mountinfo *mi = mountinfo_find(mountinfo_root, d->major, d->minor);
	if(unlikely(!mi)) {
		// mountinfo_free() can be called with NULL mountinfo_root
		mountinfo_free(mountinfo_root);

		// re-read mountinfo in case something changed
		mountinfo_root = mountinfo_read();

		// search again for this disk
		mi = mountinfo_find(mountinfo_root, d->major, d->minor);
	}

	if(mi)
		d->mount_point = strdup(mi->mount_point);
		// no need to check for NULL
	else
		d->mount_point = NULL;

	return d;
}

int do_proc_diskstats(int update_every, unsigned long long dt) {
	static procfile *ff = NULL;
	static char path_to_get_hw_sector_size[FILENAME_MAX + 1] = "";
	static int enable_new_disks = -1;
	static int do_io = -1, do_ops = -1, do_mops = -1, do_iotime = -1, do_qops = -1, do_util = -1, do_backlog = -1, do_space = -1;
	static struct statvfs buff_statvfs;
	static struct stat buff_stat;

	if(enable_new_disks == -1)	enable_new_disks = config_get_boolean_ondemand("plugin:proc:/proc/diskstats", "enable new disks detected at runtime", CONFIG_ONDEMAND_ONDEMAND);

	if(do_io == -1)		do_io 		= config_get_boolean_ondemand("plugin:proc:/proc/diskstats", "bandwidth for all disks", CONFIG_ONDEMAND_ONDEMAND);
	if(do_ops == -1)	do_ops 		= config_get_boolean_ondemand("plugin:proc:/proc/diskstats", "operations for all disks", CONFIG_ONDEMAND_ONDEMAND);
	if(do_mops == -1)	do_mops 	= config_get_boolean_ondemand("plugin:proc:/proc/diskstats", "merged operations for all disks", CONFIG_ONDEMAND_ONDEMAND);
	if(do_iotime == -1)	do_iotime 	= config_get_boolean_ondemand("plugin:proc:/proc/diskstats", "i/o time for all disks", CONFIG_ONDEMAND_ONDEMAND);
	if(do_qops == -1)	do_qops 	= config_get_boolean_ondemand("plugin:proc:/proc/diskstats", "queued operations for all disks", CONFIG_ONDEMAND_ONDEMAND);
	if(do_util == -1)	do_util 	= config_get_boolean_ondemand("plugin:proc:/proc/diskstats", "utilization percentage for all disks", CONFIG_ONDEMAND_ONDEMAND);
	if(do_backlog == -1)do_backlog 	= config_get_boolean_ondemand("plugin:proc:/proc/diskstats", "backlog for all disks", CONFIG_ONDEMAND_ONDEMAND);
	if(do_space == -1)  do_space 	= config_get_boolean_ondemand("plugin:proc:/proc/diskstats", "space usage for all disks", CONFIG_ONDEMAND_ONDEMAND);

	if(!ff) {
		char filename[FILENAME_MAX + 1];
		snprintfz(filename, FILENAME_MAX, "%s%s", global_host_prefix, "/proc/diskstats");
		ff = procfile_open(config_get("plugin:proc:/proc/diskstats", "filename to monitor", filename), " \t", PROCFILE_FLAG_DEFAULT);
	}
	if(!ff) return 1;

	if(!path_to_get_hw_sector_size[0]) {
		char filename[FILENAME_MAX + 1];
		snprintfz(filename, FILENAME_MAX, "%s%s", global_host_prefix, "/sys/block/%s/queue/hw_sector_size");
		snprintfz(path_to_get_hw_sector_size, FILENAME_MAX, "%s", config_get("plugin:proc:/proc/diskstats", "path to get h/w sector size", filename));
	}

	ff = procfile_readall(ff);
	if(!ff) return 0; // we return 0, so that we will retry to open it next time

	uint32_t lines = procfile_lines(ff), l;
	uint32_t words;

	for(l = 0; l < lines ;l++) {
		char *disk;
		unsigned long long 	major = 0, minor = 0,
							reads = 0,  mreads = 0,  readsectors = 0,  readms = 0,
							writes = 0, mwrites = 0, writesectors = 0, writems = 0,
							queued_ios = 0, busy_ms = 0, backlog_ms = 0,
							space_avail = 0, space_avail_root = 0, space_used = 0;

		unsigned long long 	last_reads = 0,  last_readsectors = 0,  last_readms = 0,
							last_writes = 0, last_writesectors = 0, last_writems = 0,
							last_busy_ms = 0;

		words = procfile_linewords(ff, l);
		if(words < 14) continue;

		major 			= strtoull(procfile_lineword(ff, l, 0), NULL, 10);
		minor 			= strtoull(procfile_lineword(ff, l, 1), NULL, 10);
		disk 			= procfile_lineword(ff, l, 2);

		// # of reads completed # of writes completed
		// This is the total number of reads or writes completed successfully.
		reads 			= strtoull(procfile_lineword(ff, l, 3), NULL, 10); 	// rd_ios
		writes 			= strtoull(procfile_lineword(ff, l, 7), NULL, 10); 	// wr_ios

		// # of reads merged # of writes merged
		// Reads and writes which are adjacent to each other may be merged for
	    // efficiency.  Thus two 4K reads may become one 8K read before it is
	    // ultimately handed to the disk, and so it will be counted (and queued)
		mreads		 	= strtoull(procfile_lineword(ff, l, 4), NULL, 10); 	// rd_merges_or_rd_sec
		mwrites 		= strtoull(procfile_lineword(ff, l, 8), NULL, 10); 	// wr_merges

		// # of sectors read # of sectors written
		// This is the total number of sectors read or written successfully.
		readsectors 	= strtoull(procfile_lineword(ff, l, 5), NULL, 10); 	// rd_sec_or_wr_ios
		writesectors 	= strtoull(procfile_lineword(ff, l, 9), NULL, 10); 	// wr_sec

		// # of milliseconds spent reading # of milliseconds spent writing
		// This is the total number of milliseconds spent by all reads or writes (as
		// measured from __make_request() to end_that_request_last()).
		readms 			= strtoull(procfile_lineword(ff, l, 6), NULL, 10); 	// rd_ticks_or_wr_sec
		writems 		= strtoull(procfile_lineword(ff, l, 10), NULL, 10);	// wr_ticks

		// # of I/Os currently in progress
		// The only field that should go to zero. Incremented as requests are
		// given to appropriate struct request_queue and decremented as they finish.
		queued_ios 		= strtoull(procfile_lineword(ff, l, 11), NULL, 10);	// ios_pgr

		// # of milliseconds spent doing I/Os
		// This field increases so long as field queued_ios is nonzero.
		busy_ms 		= strtoull(procfile_lineword(ff, l, 12), NULL, 10);	// tot_ticks

		// weighted # of milliseconds spent doing I/Os
		// This field is incremented at each I/O start, I/O completion, I/O
		// merge, or read of these stats by the number of I/Os in progress
		// (field queued_ios) times the number of milliseconds spent doing I/O since the
		// last update of this field.  This can provide an easy measure of both
		// I/O completion time and the backlog that may be accumulating.
		backlog_ms 		= strtoull(procfile_lineword(ff, l, 13), NULL, 10);	// rq_ticks

		int def_enabled = 0;

		// remove slashes from disk names
		char *s;
		for(s = disk; *s ;s++) if(*s == '/') *s = '_';

		struct disk *d = get_disk(major, minor);

		// Enable statistics for physical disks and mount points by default.
		if( (d->type == DISK_TYPE_PHYSICAL) || (d->mount_point != NULL) ) {
			def_enabled = enable_new_disks;
		}

		/*
		// Enable physical disks by default.
		// To fine out if it is a harddrive we use
		// Linux Assigned Names and Numbers Authority (http://www.lanana.org/)
		// We can't do that because proprietary disk drivers do not stick to
		// the standard. To detect re
		switch(major) {
			case 8: // SCSI disk devices (0-15)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 21: // Acorn MFM hard drive interface
				if(!(minor % 64)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 28: // ACSI disk (68k/Atari)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 36: // MCA ESDI hard disk
				if(!(minor % 64)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 48: // Mylex DAC960 PCI RAID controller; first controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 49: // Mylex DAC960 PCI RAID controller; second controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 50: // Mylex DAC960 PCI RAID controller; third controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 51: // Mylex DAC960 PCI RAID controller; fourth controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 52: // Mylex DAC960 PCI RAID controller; fifth controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 53: // Mylex DAC960 PCI RAID controller; sixth controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 54: // Mylex DAC960 PCI RAID controller; seventh controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 55: // Mylex DAC960 PCI RAID controller; eigth controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 65: // SCSI disk devices (16-31)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 66: // SCSI disk devices (32-47)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 67: // SCSI disk devices (48-63)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 68: // SCSI disk devices (64-79)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 69: // SCSI disk devices (80-95)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 70: // SCSI disk devices (96-111)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 71: // SCSI disk devices (112-127)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 80: // I2O hard disk
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 81: // I2O hard disk
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 82: // I2O hard disk
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 83: // I2O hard disk
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 84: // I2O hard disk
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 85: // I2O hard disk
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 86: // I2O hard disk
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 87: // I2O hard disk
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 128: // SCSI disk devices (128-143)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 129: // SCSI disk devices (144-159)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 130: // SCSI disk devices (160-175)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 131: // SCSI disk devices (176-191)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 132: // SCSI disk devices (192-207)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 133: // SCSI disk devices (208-223)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 134: // SCSI disk devices (224-239)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 135: // SCSI disk devices (240-255)
				if(!(minor % 16)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 136: // Mylex DAC960 PCI RAID controller; nineth controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 137: // Mylex DAC960 PCI RAID controller; tenth controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 138: // Mylex DAC960 PCI RAID controller; eleventh controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 139: // Mylex DAC960 PCI RAID controller; twelfth controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 140: // Mylex DAC960 PCI RAID controller; thirteenth controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 141: // Mylex DAC960 PCI RAID controller; fourteenth controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 142: // Mylex DAC960 PCI RAID controller; fifteenth controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 143: // Mylex DAC960 PCI RAID controller; sixteenth controller
				if(!(minor % 8)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 160: // Carmel 8-port SATA Disks on First Controller
				if(!(minor % 32)) {
					def_enabled = enable_new_disks;
				}
				break;
			case 161: // Carmel 8-port SATA Disks on Second Controller
				if(!(minor % 32)) {
					def_enabled = enable_new_disks;
				}
				break;
			default:
				def_enabled = 0;
				break;
		}
		*/

		char *mount_point = d->mount_point;
		char *family = d->mount_point;
		if(!family) family = disk;

		int ddo_io = do_io, ddo_ops = do_ops, ddo_mops = do_mops, ddo_iotime = do_iotime, ddo_qops = do_qops, ddo_util = do_util, ddo_backlog = do_backlog, ddo_space = do_space;

		// check which charts are enabled for this disk
		{
			char var_name[4096 + 1];
			snprintfz(var_name, 4096, "plugin:proc:/proc/diskstats:%s", disk);
			def_enabled = config_get_boolean_ondemand(var_name, "enabled", def_enabled);
			if(def_enabled == CONFIG_ONDEMAND_NO) continue;
			if(def_enabled == CONFIG_ONDEMAND_ONDEMAND && !reads && !writes) continue;


			ddo_io 		= config_get_boolean_ondemand(var_name, "bandwidth", ddo_io);
			ddo_ops 	= config_get_boolean_ondemand(var_name, "operations", ddo_ops);
			ddo_mops 	= config_get_boolean_ondemand(var_name, "merged operations", ddo_mops);
			ddo_iotime 	= config_get_boolean_ondemand(var_name, "i/o time", ddo_iotime);
			ddo_qops 	= config_get_boolean_ondemand(var_name, "queued operations", ddo_qops);
			ddo_util 	= config_get_boolean_ondemand(var_name, "utilization percentage", ddo_util);
			ddo_backlog = config_get_boolean_ondemand(var_name, "backlog", ddo_backlog);
			ddo_space   = config_get_boolean_ondemand(var_name, "space", ddo_space);

			// by default, do not add charts that do not have values
			if(ddo_io == CONFIG_ONDEMAND_ONDEMAND && !reads && !writes) ddo_io = 0;
			if(ddo_mops == CONFIG_ONDEMAND_ONDEMAND && mreads == 0 && mwrites == 0) ddo_mops = 0;
			if(ddo_iotime == CONFIG_ONDEMAND_ONDEMAND && readms == 0 && writems == 0) ddo_iotime = 0;
			if(ddo_util == CONFIG_ONDEMAND_ONDEMAND && busy_ms == 0) ddo_util = 0;
			if(ddo_backlog == CONFIG_ONDEMAND_ONDEMAND && backlog_ms == 0) ddo_backlog = 0;
			if(ddo_qops == CONFIG_ONDEMAND_ONDEMAND && backlog_ms == 0) ddo_qops = 0;

			// for absolute values, we need to switch the setting to 'yes'
			// to allow it refresh from now on
			if(ddo_qops == CONFIG_ONDEMAND_ONDEMAND) config_set(var_name, "queued operations", "yes");
		}

		RRDSET *st;

		// --------------------------------------------------------------------

		int sector_size = 512;
		if(ddo_io) {
			st = rrdset_find_bytype(RRD_TYPE_DISK, disk);
			if(!st) {
				char tf[FILENAME_MAX + 1], *t;
				char ssfilename[FILENAME_MAX + 1];

				strncpyz(tf, disk, FILENAME_MAX);

				// replace all / with !
				while((t = strchr(tf, '/'))) *t = '!';

				snprintfz(ssfilename, FILENAME_MAX, path_to_get_hw_sector_size, tf);
				FILE *fpss = fopen(ssfilename, "r");
				if(fpss) {
					char ssbuffer[1025];
					char *tmp = fgets(ssbuffer, 1024, fpss);

					if(tmp) {
						sector_size = atoi(tmp);
						if(sector_size <= 0) {
							error("Invalid sector size %d for device %s in %s. Assuming 512.", sector_size, disk, ssfilename);
							sector_size = 512;
						}
					}
					else error("Cannot read data for sector size for device %s from %s. Assuming 512.", disk, ssfilename);

					fclose(fpss);
				}
				else error("Cannot read sector size for device %s from %s. Assuming 512.", disk, ssfilename);

				st = rrdset_create(RRD_TYPE_DISK, disk, NULL, family, "disk.io", "Disk I/O Bandwidth", "kilobytes/s", 2000, update_every, RRDSET_TYPE_AREA);

				rrddim_add(st, "reads", NULL, sector_size, 1024, RRDDIM_INCREMENTAL);
				rrddim_add(st, "writes", NULL, sector_size * -1, 1024, RRDDIM_INCREMENTAL);
			}
			else rrdset_next_usec(st, dt);

			last_readsectors  = rrddim_set(st, "reads", readsectors);
			last_writesectors = rrddim_set(st, "writes", writesectors);
			rrdset_done(st);
		}

		// --------------------------------------------------------------------

		if(ddo_ops) {
			st = rrdset_find_bytype("disk_ops", disk);
			if(!st) {
				st = rrdset_create("disk_ops", disk, NULL, family, "disk.ops", "Disk Completed I/O Operations", "operations/s", 2001, update_every, RRDSET_TYPE_LINE);
				st->isdetail = 1;

				rrddim_add(st, "reads", NULL, 1, 1, RRDDIM_INCREMENTAL);
				rrddim_add(st, "writes", NULL, -1, 1, RRDDIM_INCREMENTAL);
			}
			else rrdset_next_usec(st, dt);

			last_reads  = rrddim_set(st, "reads", reads);
			last_writes = rrddim_set(st, "writes", writes);
			rrdset_done(st);
		}

		// --------------------------------------------------------------------

		if(ddo_qops) {
			st = rrdset_find_bytype("disk_qops", disk);
			if(!st) {
				st = rrdset_create("disk_qops", disk, NULL, family, "disk.qops", "Disk Current I/O Operations", "operations", 2002, update_every, RRDSET_TYPE_LINE);
				st->isdetail = 1;

				rrddim_add(st, "operations", NULL, 1, 1, RRDDIM_ABSOLUTE);
			}
			else rrdset_next_usec(st, dt);

			rrddim_set(st, "operations", queued_ios);
			rrdset_done(st);
		}

		// --------------------------------------------------------------------

		if(ddo_backlog) {
			st = rrdset_find_bytype("disk_backlog", disk);
			if(!st) {
				st = rrdset_create("disk_backlog", disk, NULL, family, "disk.backlog", "Disk Backlog", "backlog (ms)", 2003, update_every, RRDSET_TYPE_AREA);
				st->isdetail = 1;

				rrddim_add(st, "backlog", NULL, 1, 10, RRDDIM_INCREMENTAL);
			}
			else rrdset_next_usec(st, dt);

			rrddim_set(st, "backlog", backlog_ms);
			rrdset_done(st);
		}

		// --------------------------------------------------------------------

		if(ddo_util) {
			st = rrdset_find_bytype("disk_util", disk);
			if(!st) {
				st = rrdset_create("disk_util", disk, NULL, family, "disk.util", "Disk Utilization Time", "% of time working", 2004, update_every, RRDSET_TYPE_AREA);
				st->isdetail = 1;

				rrddim_add(st, "utilization", NULL, 1, 10, RRDDIM_INCREMENTAL);
			}
			else rrdset_next_usec(st, dt);

			last_busy_ms = rrddim_set(st, "utilization", busy_ms);
			rrdset_done(st);
		}

		// --------------------------------------------------------------------

		if(ddo_mops) {
			st = rrdset_find_bytype("disk_mops", disk);
			if(!st) {
				st = rrdset_create("disk_mops", disk, NULL, family, "disk.mops", "Disk Merged Operations", "merged operations/s", 2021, update_every, RRDSET_TYPE_LINE);
				st->isdetail = 1;

				rrddim_add(st, "reads", NULL, 1, 1, RRDDIM_INCREMENTAL);
				rrddim_add(st, "writes", NULL, -1, 1, RRDDIM_INCREMENTAL);
			}
			else rrdset_next_usec(st, dt);

			rrddim_set(st, "reads", mreads);
			rrddim_set(st, "writes", mwrites);
			rrdset_done(st);
		}

		// --------------------------------------------------------------------

		if(ddo_iotime) {
			st = rrdset_find_bytype("disk_iotime", disk);
			if(!st) {
				st = rrdset_create("disk_iotime", disk, NULL, family, "disk.iotime", "Disk Total I/O Time", "milliseconds/s", 2022, update_every, RRDSET_TYPE_LINE);
				st->isdetail = 1;

				rrddim_add(st, "reads", NULL, 1, 1, RRDDIM_INCREMENTAL);
				rrddim_add(st, "writes", NULL, -1, 1, RRDDIM_INCREMENTAL);
			}
			else rrdset_next_usec(st, dt);

			last_readms  = rrddim_set(st, "reads", readms);
			last_writems = rrddim_set(st, "writes", writems);
			rrdset_done(st);
		}

		// --------------------------------------------------------------------

		if(ddo_space && mount_point) {
			// collect space metrics using statvfs
			if (statvfs(mount_point, &buff_statvfs) < 0) {
				error("Failed checking disk space usage of %s", family);
			} else {
				// verify we collected the metrics for the right disk.
				// if not the mountpoint has changed.
				if(stat(mount_point, &buff_stat) == -1) {
				} else {
					if(major(buff_stat.st_dev) == major && minor(buff_stat.st_dev) == minor)
					{
						space_avail = buff_statvfs.f_bavail * buff_statvfs.f_bsize;
						space_avail_root = (buff_statvfs.f_bfree - buff_statvfs.f_bavail) * buff_statvfs.f_bsize;
						space_used = (buff_statvfs.f_blocks - buff_statvfs.f_bfree) * buff_statvfs.f_bsize;

						st = rrdset_find_bytype("disk_space", disk);
						if(!st) {
							st = rrdset_create("disk_space", disk, NULL, family, "disk.space", "Disk Space Usage", "GB", 2023, update_every, RRDSET_TYPE_STACKED);
							st->isdetail = 1;

							rrddim_add(st, "avail", NULL, 1, 1000*1000*1000, RRDDIM_ABSOLUTE);
							rrddim_add(st, "reserved_for_root", "reserved for root", 1, 1000*1000*1000, RRDDIM_ABSOLUTE);
							rrddim_add(st, "used" , NULL, 1, 1000*1000*1000, RRDDIM_ABSOLUTE);
						}
						else rrdset_next_usec(st, dt);

						rrddim_set(st, "avail", space_avail);
						rrddim_set(st, "reserved_for_root", space_avail_root);
						rrddim_set(st, "used", space_used);
						rrdset_done(st);
					}
				}
			}
		}

		// --------------------------------------------------------------------
		// calculate differential charts
		// only if this is not the first time we run

		if(dt) {
			if(ddo_iotime && ddo_ops) {
				st = rrdset_find_bytype("disk_await", disk);
				if(!st) {
					st = rrdset_create("disk_await", disk, NULL, family, "disk.await", "Average Completed I/O Operation Time", "ms per operation", 2005, update_every, RRDSET_TYPE_LINE);
					st->isdetail = 1;

					rrddim_add(st, "reads", NULL, 1, 1, RRDDIM_ABSOLUTE);
					rrddim_add(st, "writes", NULL, -1, 1, RRDDIM_ABSOLUTE);
				}
				else rrdset_next_usec(st, dt);

				rrddim_set(st, "reads", (reads - last_reads) ? (readms - last_readms) / (reads - last_reads) : 0);
				rrddim_set(st, "writes", (writes - last_writes) ? (writems - last_writems) / (writes - last_writes) : 0);
				rrdset_done(st);
			}

			if(ddo_io && ddo_ops) {
				st = rrdset_find_bytype("disk_avgsz", disk);
				if(!st) {
					st = rrdset_create("disk_avgsz", disk, NULL, family, "disk.avgsz", "Average Completed I/O Operation Bandwidth", "kilobytes per operation", 2006, update_every, RRDSET_TYPE_AREA);
					st->isdetail = 1;

					rrddim_add(st, "reads", NULL, sector_size, 1024, RRDDIM_ABSOLUTE);
					rrddim_add(st, "writes", NULL, -sector_size, 1024, RRDDIM_ABSOLUTE);
				}
				else rrdset_next_usec(st, dt);

				rrddim_set(st, "reads", (reads - last_reads) ? (readsectors - last_readsectors) / (reads - last_reads) : 0);
				rrddim_set(st, "writes", (writes - last_writes) ? (writesectors - last_writesectors) / (writes - last_writes) : 0);
				rrdset_done(st);
			}

			if(ddo_util && ddo_ops) {
				st = rrdset_find_bytype("disk_svctm", disk);
				if(!st) {
					st = rrdset_create("disk_svctm", disk, NULL, family, "disk.svctm", "Average Service Time", "ms per operation", 2007, update_every, RRDSET_TYPE_LINE);
					st->isdetail = 1;

					rrddim_add(st, "svctm", NULL, 1, 1, RRDDIM_ABSOLUTE);
				}
				else rrdset_next_usec(st, dt);

				rrddim_set(st, "svctm", ((reads - last_reads) + (writes - last_writes)) ? (busy_ms - last_busy_ms) / ((reads - last_reads) + (writes - last_writes)) : 0);
				rrdset_done(st);
			}
		}
	}

	return 0;
}
