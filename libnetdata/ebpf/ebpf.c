// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/utsname.h>

#include "../libnetdata.h"

char *ebpf_user_config_dir = CONFIG_DIR;
char *ebpf_stock_config_dir = LIBCONFIG_DIR;

/*
static int clean_kprobe_event(FILE *out, char *filename, char *father_pid, netdata_ebpf_events_t *ptr)
{
    int fd = open(filename, O_WRONLY | O_APPEND, 0);
    if (fd < 0) {
        if (out) {
            fprintf(out, "Cannot open %s : %s\n", filename, strerror(errno));
        }
        return 1;
    }

    char cmd[1024];
    int length = snprintf(cmd, 1023, "-:kprobes/%c_netdata_%s_%s", ptr->type, ptr->name, father_pid);
    int ret = 0;
    if (length > 0) {
        ssize_t written = write(fd, cmd, strlen(cmd));
        if (written < 0) {
            if (out) {
                fprintf(
                    out, "Cannot remove the event (%d, %d) '%s' from %s : %s\n", getppid(), getpid(), cmd, filename,
                    strerror((int)errno));
            }
            ret = 1;
        }
    }

    close(fd);

    return ret;
}

int clean_kprobe_events(FILE *out, int pid, netdata_ebpf_events_t *ptr)
{
    debug(D_EXIT, "Cleaning parent process events.");
    char filename[FILENAME_MAX + 1];
    snprintf(filename, FILENAME_MAX, "%s%s", NETDATA_DEBUGFS, "kprobe_events");

    char removeme[16];
    snprintf(removeme, 15, "%d", pid);

    int i;
    for (i = 0; ptr[i].name; i++) {
        if (clean_kprobe_event(out, filename, removeme, &ptr[i])) {
            break;
        }
    }

    return 0;
}
*/

//----------------------------------------------------------------------------------------------------------------------

int get_kernel_version(char *out, int size)
{
    char major[16], minor[16], patch[16];
    char ver[VERSION_STRING_LEN];
    char *version = ver;

    out[0] = '\0';
    int fd = open("/proc/sys/kernel/osrelease", O_RDONLY);
    if (fd < 0)
        return -1;

    ssize_t len = read(fd, ver, sizeof(ver));
    if (len < 0) {
        close(fd);
        return -1;
    }

    close(fd);

    char *move = major;
    while (*version && *version != '.')
        *move++ = *version++;
    *move = '\0';

    version++;
    move = minor;
    while (*version && *version != '.')
        *move++ = *version++;
    *move = '\0';

    if (*version)
        version++;
    else
        return -1;

    move = patch;
    while (*version && *version != '\n' && *version != '-')
        *move++ = *version++;
    *move = '\0';

    fd = snprintf(out, (size_t)size, "%s.%s.%s", major, minor, patch);
    if (fd > size)
        error("The buffer to store kernel version is not smaller than necessary.");

    return ((int)(str2l(major) * 65536) + (int)(str2l(minor) * 256) + (int)str2l(patch));
}

int get_redhat_release()
{
    char buffer[VERSION_STRING_LEN + 1];
    int major, minor;
    FILE *fp = fopen("/etc/redhat-release", "r");

    if (fp) {
        major = 0;
        minor = -1;
        size_t length = fread(buffer, sizeof(char), VERSION_STRING_LEN, fp);
        if (length > 4) {
            buffer[length] = '\0';
            char *end = strchr(buffer, '.');
            char *start;
            if (end) {
                *end = 0x0;

                if (end > buffer) {
                    start = end - 1;

                    major = strtol(start, NULL, 10);
                    start = ++end;

                    end++;
                    if (end) {
                        end = 0x00;
                        minor = strtol(start, NULL, 10);
                    } else {
                        minor = -1;
                    }
                }
            }
        }

        fclose(fp);
        return ((major * 256) + minor);
    } else {
        return -1;
    }
}

/**
 * Check if the kernel is in a list of rejected ones
 *
 * @return Returns 1 if the kernel is rejected, 0 otherwise.
 */
static int kernel_is_rejected()
{
    // Get kernel version from system
    char version_string[VERSION_STRING_LEN + 1];
    int version_string_len = 0;

    if (read_file("/proc/version_signature", version_string, VERSION_STRING_LEN)) {
        if (read_file("/proc/version", version_string, VERSION_STRING_LEN)) {
            struct utsname uname_buf;
            if (!uname(&uname_buf)) {
                info("Cannot check kernel version");
                return 0;
            }
            version_string_len =
                snprintfz(version_string, VERSION_STRING_LEN, "%s %s", uname_buf.release, uname_buf.version);
        }
    }

    if (!version_string_len)
        version_string_len = strlen(version_string);

    // Open a file with a list of rejected kernels
    char *config_dir = getenv("NETDATA_USER_CONFIG_DIR");
    if (config_dir == NULL) {
        config_dir = CONFIG_DIR;
    }

    char filename[FILENAME_MAX + 1];
    snprintfz(filename, FILENAME_MAX, "%s/ebpf.d/%s", config_dir, EBPF_KERNEL_REJECT_LIST_FILE);
    FILE *kernel_reject_list = fopen(filename, "r");

    if (!kernel_reject_list) {
        // Keep this to have compatibility with old versions
        snprintfz(filename, FILENAME_MAX, "%s/%s", config_dir, EBPF_KERNEL_REJECT_LIST_FILE);
        kernel_reject_list = fopen(filename, "r");

        if (!kernel_reject_list) {
            config_dir = getenv("NETDATA_STOCK_CONFIG_DIR");
            if (config_dir == NULL) {
                config_dir = LIBCONFIG_DIR;
            }

            snprintfz(filename, FILENAME_MAX, "%s/ebpf.d/%s", config_dir, EBPF_KERNEL_REJECT_LIST_FILE);
            kernel_reject_list = fopen(filename, "r");

            if (!kernel_reject_list)
                return 0;
        }
    }

    // Find if the kernel is in the reject list
    char *reject_string = NULL;
    size_t buf_len = 0;
    ssize_t reject_string_len;
    while ((reject_string_len = getline(&reject_string, &buf_len, kernel_reject_list) - 1) > 0) {
        if (version_string_len >= reject_string_len) {
            if (!strncmp(version_string, reject_string, reject_string_len)) {
                info("A buggy kernel is detected");
                fclose(kernel_reject_list);
                freez(reject_string);
                return 1;
            }
        }
    }

    fclose(kernel_reject_list);
    freez(reject_string);

    return 0;
}

static int has_ebpf_kernel_version(int version)
{
    if (kernel_is_rejected())
        return 0;

    // Kernel 4.11.0 or RH > 7.5
    return (version >= NETDATA_MINIMUM_EBPF_KERNEL || get_redhat_release() >= NETDATA_MINIMUM_RH_VERSION);
}

int has_condition_to_run(int version)
{
    if (!has_ebpf_kernel_version(version))
        return 0;

    return 1;
}

//----------------------------------------------------------------------------------------------------------------------

char *ebpf_kernel_suffix(int version, int isrh)
{
    if (isrh) {
        if (version >= NETDATA_EBPF_KERNEL_4_11)
            return "4.18";
        else
            return "3.10";
    } else {
        if (version >= NETDATA_EBPF_KERNEL_5_11)
            return "5.11";
        else if (version >= NETDATA_EBPF_KERNEL_5_10)
            return "5.10";
        else if (version >= NETDATA_EBPF_KERNEL_4_17)
            return "5.4";
        else if (version >= NETDATA_EBPF_KERNEL_4_15)
            return "4.16";
        else if (version >= NETDATA_EBPF_KERNEL_4_11)
            return "4.14";
    }

    return NULL;
}

//----------------------------------------------------------------------------------------------------------------------

int ebpf_update_kernel(ebpf_data_t *ed)
{
    char *kernel = ebpf_kernel_suffix(ed->running_on_kernel, (ed->isrh < 0) ? 0 : 1);
    size_t length = strlen(kernel);
    strncpyz(ed->kernel_string, kernel, length);
    ed->kernel_string[length] = '\0';

    return 0;
}

static int select_file(char *name, const char *program, size_t length, int mode, char *kernel_string)
{
    int ret = -1;
    if (!mode)
        ret = snprintf(name, length, "rnetdata_ebpf_%s.%s.o", program, kernel_string);
    else if (mode == 1)
        ret = snprintf(name, length, "dnetdata_ebpf_%s.%s.o", program, kernel_string);
    else if (mode == 2)
        ret = snprintf(name, length, "pnetdata_ebpf_%s.%s.o", program, kernel_string);

    return ret;
}

void ebpf_update_pid_table(ebpf_local_maps_t *pid, ebpf_module_t *em)
{
    pid->user_input = em->pid_map_size;
}

void ebpf_update_map_sizes(struct bpf_object *program, ebpf_module_t *em)
{
    struct bpf_map *map;
    ebpf_local_maps_t *maps = em->maps;
    if (!maps)
        return;

    bpf_map__for_each(map, program)
    {
        const char *map_name = bpf_map__name(map);
        int i = 0; ;
        while (maps[i].name) {
            ebpf_local_maps_t *w = &maps[i];
            if (w->user_input != w->internal_input && !strcmp(w->name, map_name)) {
#ifdef NETDATA_INTERNAL_CHECKS
                info("Changing map %s from size %u to %u ", map_name, w->internal_input, w->user_input);
#endif
                bpf_map__resize(map, w->user_input);
            }
            i++;
        }
    }
}

size_t ebpf_count_programs(struct bpf_object *obj)
{
    size_t tot = 0;
    struct bpf_program *prog;
    bpf_object__for_each_program(prog, obj)
    {
        tot++;
    }

    return tot;
}

static ebpf_specify_name_t *ebpf_find_names(ebpf_specify_name_t *names, const char *prog_name)
{
    size_t i = 0;
    while (names[i].program_name) {
        if (!strcmp(prog_name, names[i].program_name))
            return &names[i];

        i++;
    }

    return NULL;
}

static struct bpf_link **ebpf_attach_programs(struct bpf_object *obj, size_t length, ebpf_specify_name_t *names)
{
    struct bpf_link **links = callocz(length , sizeof(struct bpf_link *));
    size_t i = 0;
    struct bpf_program *prog;
    bpf_object__for_each_program(prog, obj)
    {
        links[i] = bpf_program__attach(prog);
        if (libbpf_get_error(links[i]) && names) {
            const char *name = bpf_program__name(prog);
            ebpf_specify_name_t *w = ebpf_find_names(names, name);
            if (w) {
                enum bpf_prog_type type = bpf_program__get_type(prog);
                if (type == BPF_PROG_TYPE_KPROBE)
                    links[i] = bpf_program__attach_kprobe(prog, w->retprobe, w->optional);
            }
        }

        if (libbpf_get_error(links[i])) {
            links[i] = NULL;
        }

        i++;
    }

    return links;
}

struct bpf_link **ebpf_load_program(char *plugins_dir, ebpf_module_t *em, char *kernel_string,
                                    struct bpf_object **obj, int *map_fd)
{
    char lpath[4096];
    char lname[128];

    int test = select_file(lname, em->thread_name, (size_t)127, em->mode, kernel_string);
    if (test < 0 || test > 127)
        return NULL;

    snprintf(lpath, 4096, "%s/ebpf.d/%s", plugins_dir, lname);
    *obj = bpf_object__open_file(lpath, NULL);
    if (libbpf_get_error(obj)) {
        error("Cannot open BPF object %s", lpath);
        bpf_object__close(*obj);
        return NULL;
    }

    ebpf_update_map_sizes(*obj, em);

    if (bpf_object__load(*obj)) {
        error("ERROR: loading BPF object file failed %s\n", lpath);
        bpf_object__close(*obj);
        return NULL;
    }

    struct bpf_map *map;
    size_t i = 0;
    bpf_map__for_each(map, *obj)
    {
        map_fd[i] = bpf_map__fd(map);
        i++;
    }

    size_t count_programs =  ebpf_count_programs(*obj);

    return ebpf_attach_programs(*obj, count_programs, em->names);
}

static char *ebpf_update_name(char *search)
{
    char filename[FILENAME_MAX + 1];
    char *ret = NULL;
    snprintfz(filename, FILENAME_MAX, "%s%s", netdata_configured_host_prefix, NETDATA_KALLSYMS);
    procfile *ff = procfile_open(filename, " \t", PROCFILE_FLAG_DEFAULT);
    if(unlikely(!ff)) {
        error("Cannot open %s%s", netdata_configured_host_prefix, NETDATA_KALLSYMS);
        return ret;
    }

    ff = procfile_readall(ff);
    if(unlikely(!ff))
        return ret;

    unsigned long i, lines = procfile_lines(ff);
    size_t length = strlen(search);
    for(i = 0; i < lines ; i++) {
        char *cmp = procfile_lineword(ff, i,2);;
        if (!strncmp(search, cmp, length)) {
            ret = strdupz(cmp);
            break;
        }
    }

    procfile_close(ff);

    return ret;
}

void ebpf_update_names(ebpf_specify_name_t *opt, ebpf_module_t *em)
{
    int mode = em->mode;
    em->names = opt;

    size_t i = 0;
    while (opt[i].program_name) {
        opt[i].retprobe = (mode == MODE_RETURN);
        opt[i].optional = ebpf_update_name(opt[i].function_to_attach);

        i++;
    }
}

//----------------------------------------------------------------------------------------------------------------------

void ebpf_mount_config_name(char *filename, size_t length, char *path, const char *config)
{
    snprintf(filename, length, "%s/ebpf.d/%s", path, config);
}

int ebpf_load_config(struct config *config, char *filename)
{
    return appconfig_load(config, filename, 0, NULL);
}


static netdata_run_mode_t ebpf_select_mode(char *mode)
{
    if (!strcasecmp(mode, "return"))
        return MODE_RETURN;
    else if  (!strcasecmp(mode, "dev"))
        return MODE_DEVMODE;

    return MODE_ENTRY;
}

void ebpf_update_module_using_config(ebpf_module_t *modules)
{
    char *mode = appconfig_get(modules->cfg, EBPF_GLOBAL_SECTION, EBPF_CFG_LOAD_MODE, EBPF_CFG_LOAD_MODE_DEFAULT);
    modules->mode = ebpf_select_mode(mode);

    modules->update_time = (int)appconfig_get_number(modules->cfg, EBPF_GLOBAL_SECTION, EBPF_CFG_UPDATE_EVERY, 1);

    modules->apps_charts = appconfig_get_boolean(modules->cfg, EBPF_GLOBAL_SECTION, EBPF_CFG_APPLICATION,
                                                 CONFIG_BOOLEAN_YES);

    modules->pid_map_size = (uint32_t)appconfig_get_number(modules->cfg, EBPF_GLOBAL_SECTION, EBPF_CFG_PID_SIZE,
                                                           modules->pid_map_size);
}


/**
 * Update module
 *
 * When this function is called, it will load the configuration file and after this
 * it updates the global information of ebpf_module.
 * If the module has specific configuration, this function will load it, but it will not
 * update the variables.
 *
 * @param em       the module structure
 */
void ebpf_update_module(ebpf_module_t *em)
{
    char filename[FILENAME_MAX+1];
    ebpf_mount_config_name(filename, FILENAME_MAX, ebpf_user_config_dir, em->config_file);
    if (!ebpf_load_config(em->cfg, filename)) {
        ebpf_mount_config_name(filename, FILENAME_MAX, ebpf_stock_config_dir, em->config_file);
        if (!ebpf_load_config(em->cfg, filename)) {
            error("Cannot load the ebpf configuration file %s", em->config_file);
            return;
        }
    }

    ebpf_update_module_using_config(em);
}
