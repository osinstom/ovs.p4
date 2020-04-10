/* Copyright (c) 2008, 2009, 2010, 2011, 2012, 2014 Nicira, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef VSWITCHD_BRIDGE_H
#define VSWITCHD_BRIDGE_H 1

#include "openvswitch/hmap.h"
#include "smap.h"
#include <stdbool.h>

struct simap;

/* Internal representation of datapath configuration table in OVSDB. */
struct datapath {
    char *type;                 /* Datapath type. */
    struct hmap ct_zones;       /* Map of 'struct ct_zone' elements, indexed
                                 * by 'zone'. */
    struct hmap_node node;      /* Node in 'all_datapaths' hmap. */
    struct smap caps;           /* Capabilities. */
    unsigned int last_used;     /* The last idl_seqno that this 'datapath'
                                 * used in OVSDB. This number is used for
                                 * garbage collection. */
};

void bridge_init(const char *remote);
void bridge_exit(bool delete_datapath);

void bridge_run(void);
void bridge_wait(void);

void bridge_get_memory_usage(struct simap *usage);

#endif /* bridge.h */
