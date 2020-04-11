#ifndef P4RT_H
#define P4RT_H 1

#include "openvswitch/thread.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct p4rt;

/* Needed for the lock annotations. */
extern struct ovs_mutex p4rt_mutex;

int p4rt_run(struct p4rt *);

int p4rt_create(const char *datapath, const char *datapath_type, struct p4rt **p4rt);

#ifdef  __cplusplus
}
#endif

#endif /* p4rt.h */
