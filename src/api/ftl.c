/* Pi-hole: A black hole for Internet advertisements
*  (c) 2019 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  API Implementation /api/ftl
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */

#include "../FTL.h"
#include "../webserver/http-common.h"
#include "../webserver/json_macros.h"
#include "routes.h"
// struct fifologData
#include "../fifo.h"
// get_FTL_db_filesize()
#include "files.h"
// get_sqlite3_version()
#include "database/common.h"
// get_number_of_queries_in_DB()
#include "database/query-table.h"
// getgrgid()
#include <grp.h>
// sysinfo()
#include <sys/sysinfo.h>
// get_blockingstatus()
#include "../setupVars.h"
// counters
#include "../shmem.h"

int api_ftl_client(struct mg_connection *conn)
{
	cJSON *json = JSON_NEW_OBJ();
	const struct mg_request_info *request = mg_get_request_info(conn);

	// Add client's IP address
	JSON_OBJ_REF_STR(json, "remote_addr", request->remote_addr);

	// Add HTTP version
	JSON_OBJ_REF_STR(json, "http_version", request->http_version);

	// Add request method
	JSON_OBJ_REF_STR(json, "method", request->request_method);

	// Add HTTP headers
	cJSON *headers = JSON_NEW_ARRAY();
	for(int i = 0; i < request->num_headers; i++)
	{
		// Add headers
		cJSON *header = JSON_NEW_OBJ();
		JSON_OBJ_REF_STR(header, "name", request->http_headers[i].name);
		JSON_OBJ_REF_STR(header, "value", request->http_headers[i].value);
		JSON_ARRAY_ADD_ITEM(headers, header);
	}
	JSON_OBJ_ADD_ITEM(json, "headers", headers);

	JSON_SEND_OBJECT(json);
}

// fifologData is allocated in shared memory for cross-fork compatibility
fifologData *fifo_log = NULL;
int api_ftl_dnsmasq_log(struct mg_connection *conn)
{
	// Verify requesting client is allowed to see this ressource
	if(check_client_auth(conn) == API_AUTH_UNAUTHORIZED)
	{
		return send_json_unauthorized(conn);
	}

	unsigned int start = 0u;
	const struct mg_request_info *request = mg_get_request_info(conn);
	if(request->query_string != NULL)
	{
		// Does the user request an ID to sent from?
		unsigned int nextID;
		if(get_uint_var(request->query_string, "nextID", &nextID))
		{
			if(nextID >= fifo_log->next_id)
			{
				// Do not return any data
				start = LOG_SIZE;
			}
			else if((fifo_log->next_id > LOG_SIZE) && nextID < (fifo_log->next_id) - LOG_SIZE)
			{
				// Requested an ID smaller than the lowest one we have
				// We return the entire buffer
				start = 0u;
			}
			else if(fifo_log->next_id >= LOG_SIZE)
			{
				// Reply with partial buffer, measure from the end
				// (the log is full)
				start = LOG_SIZE - (fifo_log->next_id - nextID);
			}
			else
			{
				// Reply with partial buffer, measure from the start
				// (the log is not yet full)
				start = nextID;
			}
		}
	}

	// Process data
	cJSON *json = JSON_NEW_OBJ();
	cJSON *log = JSON_NEW_ARRAY();
	for(unsigned int i = start; i < LOG_SIZE; i++)
	{
		if(fifo_log->timestamp[i] == 0)
		{
			// Uninitialized buffer entry
			break;
		}

		cJSON *entry = JSON_NEW_OBJ();
		JSON_OBJ_ADD_NUMBER(entry, "timestamp", fifo_log->timestamp[i]);
		JSON_OBJ_REF_STR(entry, "message", fifo_log->message[i]);
		JSON_ARRAY_ADD_ITEM(log, entry);
	}
	JSON_OBJ_ADD_ITEM(json, "log", log);
	JSON_OBJ_ADD_NUMBER(json, "nextID", fifo_log->next_id);

	// Send data
	JSON_SEND_OBJECT(json);
}

int api_ftl_database(struct mg_connection *conn)
{
	// Verify requesting client is allowed to see this ressource
	if(check_client_auth(conn) == API_AUTH_UNAUTHORIZED)
	{
		send_json_unauthorized(conn);
	}

	cJSON *json = JSON_NEW_OBJ();

	// Add database stat details
	struct stat st;
	get_database_stat(&st);
	JSON_OBJ_ADD_NUMBER(json, "size", st.st_size); // Total size, in bytes

	// File type
	char octal[5] = { 0 };
	const char *human;
	cJSON *type = JSON_NEW_OBJ();
	snprintf(octal, sizeof(octal), "%04o", (st.st_mode & S_IFMT) >> 9);
	JSON_OBJ_COPY_STR(type, "octal", octal);
	if((st.st_mode & S_IFMT) == S_IFREG)
		human = "Regular file";
	else if((st.st_mode & S_IFMT) == S_IFLNK)
		human = "Symbolic link";
	else
		human = "Unknown";
	JSON_OBJ_REF_STR(type, "human", human);
	JSON_OBJ_ADD_ITEM(json, "type", type);

	// File mode
	cJSON *mode = JSON_NEW_OBJ();
	snprintf(octal, sizeof(octal), "%03o", st.st_mode & 0x1FF);
	JSON_OBJ_COPY_STR(mode, "octal", octal);
	char permissions[10] = { 0 };
	get_permission_string(permissions, &st);
	JSON_OBJ_REF_STR(mode, "human", permissions);
	JSON_OBJ_ADD_ITEM(json, "mode", mode);

	JSON_OBJ_ADD_NUMBER(json, "atime", st.st_atime); // Time of last access
	JSON_OBJ_ADD_NUMBER(json, "mtime", st.st_mtime); // Time of last modification
	JSON_OBJ_ADD_NUMBER(json, "ctime", st.st_ctime); // Time of last status change (owner or mode change, etc.)

	// Get owner details
	cJSON *user = JSON_NEW_OBJ();
	JSON_OBJ_ADD_NUMBER(user, "uid", st.st_uid); // UID
	const struct passwd *pw = getpwuid(st.st_uid);
	if(pw != NULL)
	{
		JSON_OBJ_COPY_STR(user, "name", pw->pw_name); // User name
		JSON_OBJ_COPY_STR(user, "info", pw->pw_gecos); // User information
	}
	cJSON *group = JSON_NEW_OBJ();
	JSON_OBJ_ADD_NUMBER(group, "gid", st.st_gid); // GID
	const struct group *gr = getgrgid(st.st_uid);
	if(gr != NULL)
	{
		JSON_OBJ_COPY_STR(group, "name", gr->gr_name); // Group name
	}
	cJSON *owner = JSON_NEW_OBJ();
	JSON_OBJ_ADD_ITEM(owner, "user", user);
	JSON_OBJ_ADD_ITEM(owner, "group", group);
	JSON_OBJ_ADD_ITEM(json, "owner", owner);

	// Add number of queries in database
	const int queries_in_database = get_number_of_queries_in_DB();
	JSON_OBJ_ADD_NUMBER(json, "queries", queries_in_database);

	// Add SQLite library version
	JSON_OBJ_REF_STR(json, "sqlite_version", get_sqlite3_version());

	// Send reply to user
	JSON_SEND_OBJECT(json);
}

static int read_temp_sensor(struct mg_connection *conn,
                            const char *label_path,
                            const char *value_path,
                            const char *fallback_label,
                            cJSON *object)
{
	FILE *f_label = fopen(label_path, "r");
	FILE *f_value = fopen(value_path, "r");
	if(f_value != NULL)
	{
		int temp = 0;
		char label[1024];
		if(fscanf(f_value, "%d", &temp) == 1)
		{
			cJSON *item = JSON_NEW_OBJ();
			if(f_label != NULL && fread(label, sizeof(label)-1, 1, f_label) > 0)
			{
				JSON_OBJ_COPY_STR(item, "name", label);
			}
			else
			{
				JSON_OBJ_COPY_STR(item, "name", fallback_label);
			}
			JSON_OBJ_ADD_NUMBER(item, "value", temp < 1000 ? temp : 1e-3f*temp);
			JSON_ARRAY_ADD_ITEM(object, item);
		}
	}
	if(f_label != NULL)
		fclose(f_label);
	if(f_value != NULL)
		fclose(f_value);

	return 0;
}

// Get RAM information in units of kB
// This is implemented similar to how free (procps) does it
static bool GetRamInKB(long *mem_total, long *mem_used, long *mem_free, long *mem_avail)
{
	long page_cached = -1, buffers = -1, slab_reclaimable = -1;
	FILE *meminfo = fopen("/proc/meminfo", "r");
	if(meminfo == NULL)
		return false;

	char line[256];
	while(fgets(line, sizeof(line), meminfo))
	{
		sscanf(line, "MemTotal: %ld kB", mem_total);
		sscanf(line, "MemFree: %ld kB", mem_free);
		sscanf(line, "MemAvailable: %ld kB", mem_avail);
		sscanf(line, "Cached: %ld kB", &page_cached);
		sscanf(line, "Buffers: %ld kB", &buffers);
		sscanf(line, "SReclaimable: %ld kB", &slab_reclaimable);

		// Exit if we have them all
		if(*mem_total > -1 && *mem_avail > -1 && *mem_free > -1 &&
		   buffers > -1 && slab_reclaimable > -1)
			break;
	}
	fclose(meminfo);

	// Compute actual memory numbers
	const long mem_cached = page_cached + slab_reclaimable;
	// if mem_avail is greater than mem_total or our calculation of used
	// overflows, that's symptomatic of running within a lxc container where
	// such values will be dramatically distorted over those of the host.
	if (*mem_avail > *mem_total)
		*mem_avail = *mem_free;
	*mem_used = *mem_total - *mem_free - mem_cached - buffers;
	if (*mem_used < 0)
		*mem_used = *mem_total - *mem_free;

	// Return success
	return true;
}

int get_system_obj(struct mg_connection *conn, cJSON *system)
{
	const int nprocs = get_nprocs();
	struct sysinfo info;
	if(sysinfo(&info) != 0)
		return send_json_error(conn, 500, "error", strerror(errno), NULL);

	// Seconds since boot
	JSON_OBJ_ADD_NUMBER(system, "uptime", info.uptime);

	cJSON *memory = JSON_NEW_OBJ();
	cJSON *ram = JSON_NEW_OBJ();
	// We cannot use the memory information available through sysinfo() as
	// this is not what we want. It is worth noting that freeram in sysinfo
	// is not what most people would call "free RAM". freeram excludes
	// memory used by cached filesystem metadata ("buffers") and contents
	// ("cache"). Both of these can be a significant portion of RAM but are
	// freed by the OS when programs need that memory. sysinfo does contain
	// size used by buffers (sysinfo.bufferram), but not cache. The best
	// option is to use the MemAvailable (as opposed to MemFree) entry in
	// /proc/meminfo instead.
	long mem_total = -1, mem_used = -1, mem_free = -1, mem_avail = -1;
	GetRamInKB(&mem_total, &mem_used, &mem_free, &mem_avail);	
	// Total usable main memory size
	JSON_OBJ_ADD_NUMBER(ram, "total", mem_total);
	// Used memory size
	JSON_OBJ_ADD_NUMBER(ram, "used", mem_used);
	// Free memory size
	JSON_OBJ_ADD_NUMBER(ram, "free", mem_free);
	// Available memory size
	// See https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=34e431b0ae398fc54ea69ff85ec700722c9da773
	// This Linux kernel commit message explains there are more nuances. It
	// says: "Many programs check /proc/meminfo to estimate how much free
	// memory is available. They generally do this by adding up "free" and
	// "cached", which was fine ten years ago, but is pretty much guaranteed
	// to be wrong today." 
	JSON_OBJ_ADD_NUMBER(ram, "available", mem_avail);
	JSON_OBJ_ADD_ITEM(memory, "ram", ram);

	cJSON *swap = JSON_NEW_OBJ();
	// Total swap space size
	JSON_OBJ_ADD_NUMBER(swap, "total", info.totalswap * info.mem_unit);
	// Swap space still available
	JSON_OBJ_ADD_NUMBER(swap, "free", info.freeswap * info.mem_unit);
	// Used swap space
	JSON_OBJ_ADD_NUMBER(swap, "used", (info.totalswap - info.freeswap) * info.mem_unit);
	JSON_OBJ_ADD_ITEM(memory, "swap", swap);
	JSON_OBJ_ADD_ITEM(system, "memory", memory);

	// Number of current processes
	JSON_OBJ_ADD_NUMBER(system, "procs", info.procs);

	cJSON *cpu = JSON_NEW_OBJ();
	// Number of available processors
	JSON_OBJ_ADD_NUMBER(cpu, "nprocs", nprocs);

	// 1, 5, and 15 minute load averages (we need to convert them)
	cJSON *raw = JSON_NEW_ARRAY();
	cJSON *percent = JSON_NEW_ARRAY();
	float load_f[3] = { 0.f };
	const float longfloat = 1.f / (1 << SI_LOAD_SHIFT);
	for(unsigned int i = 0; i < 3; i++)
	{
		load_f[i] = longfloat * info.loads[i];
		JSON_ARRAY_ADD_NUMBER(raw, load_f[i]);
		JSON_ARRAY_ADD_NUMBER(percent, (100.f*load_f[i]/nprocs));
	}

	// Averaged CPU usage in percent
	cJSON *load = JSON_NEW_OBJ();
	JSON_OBJ_ADD_ITEM(load, "raw", raw);
	JSON_OBJ_ADD_ITEM(load, "percent", percent);
	JSON_OBJ_ADD_ITEM(cpu, "load", load);
	JSON_OBJ_ADD_ITEM(system, "cpu", cpu);

	// Source available temperatures, we try to read as many
	// temperature sensors as there are cores on this system
	cJSON *sensors = JSON_NEW_ARRAY();
	char label_path[256], value_path[256], fallback_label[64];
	int ret;
	for(int i = 0; i < nprocs; i++)
	{
		// Try /sys/class/thermal/thermal_zoneX/{type,temp}
		sprintf(label_path, "/sys/class/thermal/thermal_zone%d/type", i);
		sprintf(value_path, "/sys/class/thermal/thermal_zone%d/temp", i);
		sprintf(fallback_label, "thermal_zone%d/temp", i);
		ret = read_temp_sensor(conn, label_path, value_path, fallback_label, sensors);
		// Error handling
		if(ret != 0)
			return ret;

		// Try /sys/class/hwmon/hwmon0X/tempX_{label,input}
		sprintf(label_path, "/sys/class/hwmon/hwmon0/temp%d_label", i);
		sprintf(value_path, "/sys/class/hwmon/hwmon0/temp%d_input", i);
		sprintf(fallback_label, "hwmon0/temp%d", i);
		ret = read_temp_sensor(conn, label_path, value_path, fallback_label, sensors);
		// Error handling
		if(ret != 0)
			return ret;
	}
	JSON_OBJ_ADD_ITEM(system, "sensors", sensors);

	cJSON *dns = JSON_NEW_OBJ();
	const bool blocking = get_blockingstatus();
	JSON_OBJ_ADD_BOOL(dns, "blocking", blocking); // same reply type as in /api/dns/status
	JSON_OBJ_ADD_NUMBER(dns, "gravity_size", counters->gravity);
	JSON_OBJ_ADD_ITEM(system, "dns", dns);

	return 0;
}

int api_ftl_system(struct mg_connection *conn)
{
	cJSON *json = JSON_NEW_OBJ();
	cJSON *system = JSON_NEW_OBJ();

	// Get system object
	const int ret = get_system_obj(conn, system);
	if (ret != 0)
		return ret;

	JSON_OBJ_ADD_ITEM(json, "system", system);
	JSON_SEND_OBJECT(json);
}