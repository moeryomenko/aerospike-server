/*
 * stats.h
 *
 * Copyright (C) 2016-2019 Aerospike, Inc.
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

#pragma once

//==========================================================
// Includes.
//

#include <stdint.h>

#include "hist.h"

#include "base/proto.h"
#include "fabric/fabric.h"


//==========================================================
// Typedefs & constants.
//

typedef struct as_stats_s {

	// Connection stats.
	uint64_t		proto_connections_opened; // not just a statistic
	uint64_t		proto_connections_closed; // not just a statistic
	// In ticker but not collected via info:
	uint64_t		heartbeat_connections_opened;
	uint64_t		heartbeat_connections_closed;
	uint64_t		fabric_connections_opened;
	uint64_t		fabric_connections_closed;

	// Heartbeat stats.
	uint64_t		heartbeat_received_self;
	uint64_t		heartbeat_received_foreign;

	// Demarshal stats.
	uint64_t		reaper_count; // not in ticker - incremented only in reaper thread

	// Info stats.
	uint64_t		info_complete;

	// Early transaction errors.
	uint64_t		n_demarshal_error;
	uint64_t		n_tsvc_client_error;
	uint64_t		n_tsvc_from_proxy_error;
	uint64_t		n_tsvc_batch_sub_error;
	uint64_t		n_tsvc_from_proxy_batch_sub_error;
	uint64_t		n_tsvc_udf_sub_error;
	uint64_t		n_tsvc_ops_sub_error;

	// Batch-index stats.
	uint64_t		batch_index_initiate; // not in ticker - not just a statistic
	uint64_t		batch_index_complete;
	uint64_t		batch_index_errors;
	uint64_t		batch_index_timeout;
	uint64_t		batch_index_delay;

	// Batch-index buffer stats.
	uint64_t		batch_index_huge_buffers; // not in ticker
	uint64_t		batch_index_created_buffers; // not in ticker
	uint64_t		batch_index_destroyed_buffers; // not in ticker

	// Batch-index proto compression stats.
	as_proto_comp_stat batch_comp_stat; // relevant only for enterprise edition

	// Fabric stats.
	uint64_t		fabric_bulk_s_rate;
	uint64_t		fabric_bulk_r_rate;
	uint64_t		fabric_ctrl_s_rate;
	uint64_t		fabric_ctrl_r_rate;
	uint64_t		fabric_meta_s_rate;
	uint64_t		fabric_meta_r_rate;
	uint64_t		fabric_rw_s_rate;
	uint64_t		fabric_rw_r_rate;

	//--------------------------------------------
	// Histograms.
	//

	histogram*		batch_index_hist;
	bool			batch_index_hist_active; // automatically activated

	histogram*		info_hist;

	histogram*		fabric_send_init_hists[AS_FABRIC_N_CHANNELS];
	histogram*		fabric_send_fragment_hists[AS_FABRIC_N_CHANNELS];
	histogram*		fabric_recv_fragment_hists[AS_FABRIC_N_CHANNELS];
	histogram*		fabric_recv_cb_hists[AS_FABRIC_N_CHANNELS];

} as_stats;


//==========================================================
// Public API.
//

// For now this is in thr_info.c, until a separate .c file is worth it.
extern as_stats g_stats;
