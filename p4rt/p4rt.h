#ifndef P4RT_H
#define P4RT_H 1

#ifdef  __cplusplus
extern "C" {
#endif

struct p4rt;

int p4rt_run(struct p4rt *);

int p4rt_create(const char *datapath, const char *datapath_type, struct p4rt **p4rt);

#ifdef  __cplusplus
}
#endif

#endif /* p4rt.h */
