#include "openvswitch/hmap.h"
#include "hash.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(p4rt);

struct p4rt {
    struct hmap_node hmap_node; /* In global 'all_p4rts' hmap. */
    char *type;                 /* Datapath type. */
    char *name;                 /* Datapath name. */
};

/* Map from datapath name to struct p4rt, for use by unixctl commands. */
static struct hmap all_p4rts = HMAP_INITIALIZER(&all_p4rts);

int p4rt_run(struct p4rt *p4rt)
{
    return 0;
}

int p4rt_create(const char *datapath_name, const char *datapath_type, struct p4rt **p4rtp)
{
    VLOG_INFO("Creating P4rt bridge");
    *p4rtp = NULL;
    struct p4rt *p4rt = xzalloc(sizeof(struct p4rt));

    /* Initialize. */
    // TODO: should lock mutex here
    memset(p4rt, 0, sizeof *p4rt);
    p4rt->name = xstrdup(datapath_name);
    p4rt->type = xstrdup(datapath_type);
    hmap_insert(&all_p4rts, &p4rt->hmap_node,
                hash_string(p4rt->name, 0));
    // TODO: should unlock mutex here
    *p4rtp = p4rt;
    return 0;
}