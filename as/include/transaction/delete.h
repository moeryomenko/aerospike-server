/*
 * delete.h
 *
 * Copyright (C) 2016 Aerospike, Inc.
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

#include <stdbool.h>

#include "base/transaction.h"


//==========================================================
// Forward declarations.
//

struct as_index_ref_s;
struct as_partition_reservation_s;
struct as_transaction_s;
struct as_namespace_s;
struct rw_request_s;


//==========================================================
// Public API.
//

transaction_status as_delete_start(struct as_transaction_s* tr);


//==========================================================
// Private API - for enterprise separation only.
//

bool delete_storage_overloaded(struct as_transaction_s* tr);
transaction_status delete_master(struct as_transaction_s* tr, struct rw_request_s* rw);
transaction_status drop_master(struct as_transaction_s* tr, struct as_index_ref_s* r_ref, struct rw_request_s* rw);
bool drop_local(struct as_namespace_s* ns, struct as_partition_reservation_s* rsv, struct as_index_ref_s* r_ref);
