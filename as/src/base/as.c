/*
 * as.c
 *
 * Copyright (C) 2008-2021 Aerospike, Inc.
 *
 * Portions may be licensed to Aerospike, Inc. under one or more contributor
 * license agreements.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 */

//==========================================================
// Includes.
//

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>
#include <sys/stat.h>

#include "citrusleaf/alloc.h"

#include "cf_thread.h"
#include "daemon.h"
#include "dns.h"
#include "fips.h"
#include "hardware.h"
#include "log.h"
#include "os.h"
#include "tls.h"

#include "base/batch.h"
#include "base/cfg.h"
#include "base/datamodel.h"
#include "base/health.h"
#include "base/index.h"
#include "base/json_init.h"
#include "base/monitor.h"
#include "base/nsup.h"
#include "base/security.h"
#include "base/service.h"
#include "base/set_index.h"
#include "base/smd.h"
#include "base/stats.h"
#include "base/thr_info.h"
#include "base/thr_info_port.h"
#include "base/ticker.h"
#include "base/truncate.h"
#include "base/xdr.h"
#include "fabric/clustering.h"
#include "fabric/exchange.h"
#include "fabric/fabric.h"
#include "fabric/hb.h"
#include "fabric/migrate.h"
#include "fabric/roster.h"
#include "fabric/skew_monitor.h"
#include "query/query.h"
#include "sindex/sindex.h"
#include "storage/storage.h"
#include "transaction/proxy.h"
#include "transaction/rw_request_hash.h"
#include "transaction/udf.h"


//==========================================================
// Typedefs & constants.
//

// String constants in version.c, generated by make.
extern const char aerospike_build_type[];
extern const char aerospike_build_id[];

// Command line options for the Aerospike server.
static const struct option CMD_OPTS[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "version", no_argument, NULL, 'v' },
		{ "config-file", required_argument, NULL, 'f' },
		{ "foreground", no_argument, NULL, 'd' },
		{ "fgdaemon", no_argument, NULL, 'F' },
		{ "early-verbose", no_argument, NULL, 'e' },
		{ "cold-start", no_argument, NULL, 'c' },
		{ "instance", required_argument, NULL, 'n' },
		{ NULL, 0, NULL, 0 }
};

static const char HELP[] =
		"\n"
		"asd informative command-line options:\n"
		"\n"
		"--help"
		"\n"
		"Print this message and exit.\n"
		"\n"
		"--version"
		"\n"
		"Print edition and build version information and exit.\n"
		"\n"
		"asd runtime command-line options:\n"
		"\n"
		"--config-file <file>"
		"\n"
		"Specify the location of the Aerospike server config file. If this option is not\n"
		"specified, the default location /etc/aerospike/aerospike.conf is used.\n"
		"\n"
		"--foreground"
		"\n"
		"Specify that Aerospike not be daemonized. This is useful for running Aerospike\n"
		"in gdb. Alternatively, add 'run-as-daemon false' in the service context of the\n"
		"Aerospike config file.\n"
		"\n"
		"--fgdaemon"
		"\n"
		"Specify that Aerospike is to be run as a \"new-style\" (foreground) daemon. This\n"
		"is useful for running Aerospike under systemd or Docker.\n"
		"\n"
		"--early-verbose"
		"\n"
		"Show verbose logging before config parsing.\n"
		"\n"
		"--cold-start"
		"\n"
		"(Enterprise edition only.) At startup, force the Aerospike server to read all\n"
		"records from storage devices to rebuild the index.\n"
		"\n"
		"--instance <0-15>"
		"\n"
		"(Enterprise edition only.) If running multiple instances of Aerospike on one\n"
		"machine (not recommended), each instance must be uniquely designated via this\n"
		"option.\n"
		;

static const char USAGE[] =
		"\n"
		"asd informative command-line options:\n"
		"[--help]\n"
		"[--version]\n"
		"\n"
		"asd runtime command-line options:\n"
		"[--config-file <file>] "
		"[--foreground] "
		"[--fgdaemon] "
		"[--early-verbose] "
		"[--cold-start] "
		"[--instance <0-15>]\n"
		;

static const char DEFAULT_CONFIG_FILE[] = "/etc/aerospike/aerospike.conf";

static const char SMD_DIR_NAME[] = "/smd";


//==========================================================
// Globals.
//

// Not cf_mutex, which won't tolerate unlock if already unlocked.
pthread_mutex_t g_main_deadlock = PTHREAD_MUTEX_INITIALIZER;

bool g_startup_complete = false;
bool g_shutdown_started = false;


//==========================================================
// Forward declarations.
//

// signal.c doesn't have header file.
extern void as_signal_setup();

static void write_pidfile(char *pidfile);
static void validate_directory(const char *path, const char *log_tag);
static void validate_smd_directory();


//==========================================================
// Public API - Aerospike server entry point.
//

int
as_run(int argc, char **argv)
{
	g_start_sec = cf_get_seconds();

	int opt;
	int opt_i;
	const char *config_file = DEFAULT_CONFIG_FILE;
	bool run_in_foreground = false;
	bool new_style_daemon = false;
	bool early_verbose = false;
	bool cold_start_cmd = false;
	uint32_t instance = 0;

	// Parse command line options.
	while ((opt = getopt_long(argc, argv, "", CMD_OPTS, &opt_i)) != -1) {
		switch (opt) {
		case 'h':
			// printf() since we want stdout and don't want cf_log's prefix.
			printf("%s\n", HELP);
			return 0;
		case 'v':
			// printf() since we want stdout and don't want cf_log's prefix.
			printf("%s build %s\n", aerospike_build_type, aerospike_build_id);
			return 0;
		case 'f':
			config_file = cf_strdup(optarg);
			break;
		case 'F':
			// As a "new-style" daemon(*), asd runs in the foreground and
			// ignores the following configuration items:
			//  - user ('user')
			//	- group ('group')
			//  - PID file ('pidfile')
			//
			// If ignoring configuration items, or if the 'console' sink is not
			// specified, warnings will appear in stderr.
			//
			// (*) http://0pointer.de/public/systemd-man/daemon.html#New-Style%20Daemons
			run_in_foreground = true;
			new_style_daemon = true;
			break;
		case 'd':
			run_in_foreground = true;
			break;
		case 'e':
			early_verbose = true;
			break;
		case 'c':
			cold_start_cmd = true;
			break;
		case 'n':
			instance = (uint32_t)strtol(optarg, NULL, 0);
			break;
		default:
			// fprintf() since we don't want cf_log's prefix.
			fprintf(stderr, "%s\n", USAGE);
			return 1;
		}
	}

	// Initializations before config parsing.
	cf_log_init(early_verbose);
	cf_alloc_init();
	cf_thread_init();
	as_signal_setup();
	cf_fips_init();
	tls_check_init();

	// Set all fields in the global runtime configuration instance. This parses
	// the configuration file, and creates as_namespace objects. (Return value
	// is a shortcut pointer to the global runtime configuration instance.)
	as_config *c = as_config_init(config_file);

	// Detect NUMA topology and, if requested, prepare for CPU and NUMA pinning.
	cf_topo_config(c->auto_pin, (cf_topo_numa_node_index)instance,
			&c->service.bind);

	// Perform privilege separation as necessary. If configured user & group
	// don't have root privileges, all resources created or reopened past this
	// point must be set up so that they are accessible without root privileges.
	// If not, the process will self-terminate with (hopefully!) a log message
	// indicating which resource is not set up properly.
	cf_process_privsep(c->uid, c->gid);

	//
	// All resources such as files, devices, and shared memory must be created
	// or reopened below this line! (The configuration file is the only thing
	// that must be opened above, in order to parse the user & group.)
	//==========================================================================

	// Activate log sinks. Up to this point, 'cf_' log output goes to stderr,
	// filtered according to early_verbose. After this point, 'cf_' log output
	// will appear in all log file sinks specified in configuration, with
	// specified filtering. If console sink is specified in configuration, 'cf_'
	// log output will continue going to stderr, but filtering will switch to
	// that specified in console sink configuration.
	cf_log_activate_sinks();

	// Daemonize asd if specified. After daemonization, output to stderr will no
	// longer appear in terminal. Instead, check /tmp/aerospike-console.<pid>
	// for console output.
	if (! run_in_foreground && c->run_as_daemon) {
		cf_process_daemonize();
	}

	// Log which build this is - should be the first line in the log file.
	cf_info(AS_AS, "<><><><><><><><><><>  %s build %s  <><><><><><><><><><>",
			aerospike_build_type, aerospike_build_id);

	// Includes echoing the configuration file to log.
	as_config_post_process(c, config_file);

	// If we allocated a non-default config file name, free it.
	if (config_file != DEFAULT_CONFIG_FILE) {
		cf_free((void*)config_file);
	}

	// Write the pid file, if specified.
	if (! new_style_daemon) {
		write_pidfile(c->pidfile);
	}
	else {
		if (c->pidfile != NULL) {
			cf_warning(AS_AS, "will not write PID file in new-style daemon mode");
		}
	}

	// Check that required directories are set up properly.
	validate_directory(c->work_directory, "work");
	validate_directory(c->mod_lua.user_path, "Lua user");
	validate_smd_directory();

	// Initialize subsystems. At this point we're allocating local resources,
	// starting worker threads, etc. (But no communication with other server
	// nodes or clients yet.)

	as_json_init();				// Jansson JSON API used by System Metadata
	as_index_tree_gc_init();	// thread to purge dropped index trees
	as_nsup_init();				// load previous evict-void-time(s)
	as_xdr_init();				// load persisted last-ship-time(s)
	as_roster_init();			// load roster-related SMD

	// Set up namespaces. Each namespace decides here whether it will do a warm
	// or cold start. Index arenas, set and bin name vmaps are initialized.
	as_namespaces_setup(cold_start_cmd, instance);

	// These load SMD involving sets/bins, needed during storage init/load.
	as_sindex_init();
	as_truncate_init();

	// Initialize namespaces. Partition structures and index tree structures are
	// initialized.
	as_namespaces_init(cold_start_cmd, instance);

	// Initialize the storage system. For warm and cool restarts, this includes
	// fully resuming persisted indexes.
	as_storage_init();
	// ... This could block for minutes ....................

	// For warm and cool restarts, fully resume persisted sindexes.
	as_sindex_resume();
	// ... This could block for minutes ....................

	// Migrate memory to correct NUMA node (includes resumed index arenas).
	cf_topo_migrate_memory();

	// Drop capabilities that we kept only for initialization.
	cf_process_drop_startup_caps();

	// For cold starts and cool restarts, this does full drive scans. (Also
	// populates data-in-memory namespaces' secondary indexes.)
	as_storage_load();
	// ... This could block for hours ......................

	// Populate data-not-in-memory namespaces' secondary indexes.
	as_sindex_load();
	// ... This could block for a while ....................

	// The defrag subsystem starts operating here. Wait for enough available
	// storage.
	as_storage_activate();
	// ... This could block for a while ....................

	cf_info(AS_AS, "initializing services...");

	cf_dns_init();				// DNS resolver
	as_security_init();			// security features
	as_service_init();			// server may process internal transactions
	as_hb_init();				// inter-node heartbeat
	as_skew_monitor_init();		// clock skew monitor
	as_fabric_init();			// inter-node communications
	as_exchange_init();			// initialize the cluster exchange subsystem
	as_clustering_init();		// clustering-v5 start
	as_info_init();				// info transaction handling
	as_migrate_init();			// move data between nodes
	as_proxy_init();			// do work on behalf of others
	as_rw_init();				// read & write service
	as_query_init();			// query transaction handling
	as_udf_init();				// user-defined functions
	as_batch_init();			// batch transaction handling
	as_mon_init();				// monitor
	as_set_index_init();		// dynamic set-index population

	// Start subsystems. At this point we may begin communicating with other
	// cluster nodes, and ultimately with clients.

	as_sindex_start();			// starts sindex GC threads
	as_smd_start();				// enables receiving cluster state change events
	as_health_start();			// starts before fabric and hb to capture them
	as_fabric_start();			// may send & receive fabric messages
	as_xdr_start();				// XDR should start before it joins other nodes
	as_hb_start();				// start inter-node heartbeat
	as_exchange_start();		// start the cluster exchange subsystem
	as_clustering_start();		// clustering-v5 start
	as_nsup_start();			// may send evict-void-time(s) to other nodes
	as_service_start();			// server will now receive client transactions
	as_info_port_start();		// server will now receive info transactions
	as_ticker_start();			// only after everything else is started

	// Relevant for enterprise edition only.
	as_storage_start_tomb_raider();

	// Log a service-ready message.
	cf_info(AS_AS, "service ready: soon there will be cake!");

	//--------------------------------------------
	// Startup is done. This thread will now wait
	// quietly for a shutdown signal.
	//

	// Stop this thread from finishing. Intentionally deadlocking on a mutex is
	// a remarkably efficient way to do this.
	pthread_mutex_lock(&g_main_deadlock);
	g_startup_complete = true;
	pthread_mutex_lock(&g_main_deadlock);

	// When the service is running, you are here (deadlocked) - the signals that
	// stop the service (yes, these signals always occur in this thread) will
	// unlock the mutex, allowing us to continue.

	g_shutdown_started = true;
	pthread_mutex_unlock(&g_main_deadlock);
	pthread_mutex_destroy(&g_main_deadlock);

	//--------------------------------------------
	// Received a shutdown signal.
	//

	cf_info(AS_AS, "initiating clean shutdown ...");

	// If this node was not quiesced and storage shutdown takes very long (e.g.
	// flushing pmem index), best to get kicked out of the cluster quickly.
	as_hb_shutdown();

	// Make sure committed SMD files are in sync with SMD callback activity.
	as_smd_shutdown();

	if (! as_storage_shutdown(instance)) {
		cf_warning(AS_AS, "failed clean shutdown - exiting");
		_exit(1);
	}

	cf_info(AS_AS, "finished clean shutdown - exiting");

	// If shutdown was totally clean (all threads joined) we could just return,
	// but for now we exit to make sure all threads die.
#ifdef DOPROFILE
	exit(0); // exit(0) so profile build actually dumps gmon.out
#else
	_exit(0);
#endif

	return 0;
}


//==========================================================
// Local helpers.
//

static void
write_pidfile(char *pidfile)
{
	if (pidfile == NULL) {
		// If there's no pid file specified in the config file, just move on.
		return;
	}

	// Note - the directory the pid file is in must already exist.

	remove(pidfile);

	int pid_fd = open(pidfile, O_CREAT | O_RDWR, cf_os_log_perms());

	if (pid_fd < 0) {
		cf_crash_nostack(AS_AS, "failed to open pid file %s: %s", pidfile,
				cf_strerror(errno));
	}

	char pidstr[16];
	sprintf(pidstr, "%u\n", (uint32_t)getpid());

	// If we can't access this resource, just log a warning and continue -
	// it is not critical to the process.
	if (write(pid_fd, pidstr, strlen(pidstr)) == -1) {
		cf_warning(AS_AS, "failed write to pid file %s: %s", pidfile,
				cf_strerror(errno));
	}

	close(pid_fd);
}

static void
validate_directory(const char *path, const char *log_tag)
{
	struct stat buf;

	if (stat(path, &buf) != 0) {
		cf_crash_nostack(AS_AS, "%s directory '%s' is not set up properly: %s",
				log_tag, path, cf_strerror(errno));
	}
	else if (! S_ISDIR(buf.st_mode)) {
		cf_crash_nostack(AS_AS, "%s directory '%s' is not set up properly: Not a directory",
				log_tag, path);
	}
}

static void
validate_smd_directory()
{
	size_t len = strlen(g_config.work_directory);
	char smd_path[len + sizeof(SMD_DIR_NAME)];

	strcpy(smd_path, g_config.work_directory);
	strcpy(smd_path + len, SMD_DIR_NAME);
	validate_directory(smd_path, "system metadata");
}
