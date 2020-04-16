#include <config.h>
#include <errno.h>

#include "p4rt.h"
#include "p4rt-provider.h"
#include "openvswitch/hmap.h"
#include "hash.h"
#include "openvswitch/vlog.h"
#include "ovs-rcu.h"
#include "sset.h"

VLOG_DEFINE_THIS_MODULE(p4rt);

/* Map from datapath name to struct p4rt, for use by unixctl commands. */
static struct hmap all_p4rts = HMAP_INITIALIZER(&all_p4rts);

static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);

/* All registered p4rt classes, in probe order. */
static const struct p4rt_class **p4rt_classes;
static size_t n_p4rt_classes;
static size_t allocated_p4rt_classes;


/* Registers a new p4rt class.  After successful registration, new p4rts
 * of that type can be created using p4rt_create(). */
int
p4rt_class_register(const struct p4rt_class *new_class)
{
    size_t i;

    for (i = 0; i < n_p4rt_classes; i++) {
        if (p4rt_classes[i] == new_class) {
            return EEXIST;
        }
    }

    if (n_p4rt_classes >= allocated_p4rt_classes) {
        p4rt_classes = x2nrealloc(p4rt_classes,
                &allocated_p4rt_classes, sizeof *p4rt_classes);
    }
    p4rt_classes[n_p4rt_classes++] = new_class;
    return 0;
}

/* Clears 'types' and enumerates all registered p4rt types into it.  The
 * caller must first initialize the sset. */
void
p4rt_enumerate_types(struct sset *types)
{
    size_t i;

    sset_clear(types);
    for (i = 0; i < n_p4rt_classes; i++) {
        p4rt_classes[i]->enumerate_types(types);
    }
}

void
p4rt_init()
{
    p4rt_class_register(&p4rt_dpif_class);
    size_t i;
    for (i = 0; i < n_p4rt_classes; i++) {
        p4rt_classes[i]->init();
    }
}

int
p4rt_run(struct p4rt *p4rt)
{
    return 0;
}

int
p4rt_create(const char *datapath_name, const char *datapath_type,
            struct p4rt **p4rtp)
    OVS_EXCLUDED(p4rt_mutex)
{
    int error;
    VLOG_INFO("Creating P4rt bridge");
    *p4rtp = NULL;
    struct p4rt *p4rt = xzalloc(sizeof(struct p4rt));

    /* Initialize. */
//    ovs_mutex_lock(&p4rt_mutex);
    memset(p4rt, 0, sizeof *p4rt);
    
    p4rt->name = xstrdup(datapath_name);
    p4rt->type = xstrdup(datapath_type);
    hmap_insert(&all_p4rts, &p4rt->hmap_node,
                hash_string(p4rt->name, 0));
//    ovs_mutex_unlock(&p4rt_mutex);

    error = p4rt->p4rt_class->construct(p4rt);

    *p4rtp = p4rt;
    return error;
}

static void
p4rt_destroy__(struct p4rt *p)
    OVS_EXCLUDED(p4rt_mutex)
{
//    ovs_mutex_lock(&p4rt_mutex);
    hmap_remove(&all_p4rts, &p->hmap_node);
//    ovs_mutex_unlock(&p4rt_mutex);

    free(p->name);
    free(p->type);
}

static void
p4rt_destroy_defer__(struct p4rt *p)
    OVS_EXCLUDED(p4rt_mutex)
{
    ovsrcu_postpone(p4rt_destroy__, p);
}

void
p4rt_destroy(struct p4rt *p, bool del)
{
    if (!p) {
        return;
    }

    /* Destroying rules is deferred, must have 'p4rt' around for them. */
    ovsrcu_postpone(p4rt_destroy_defer__, p);
}

static const struct p4rt_class *
p4rt_class_find__(const char *type)
{
    size_t i;

    for (i = 0; i < n_p4rt_classes; i++) {
        const struct p4rt_class *class = p4rt_classes[i];
        struct sset types;
        bool found;

        sset_init(&types);
        class->enumerate_types(&types);
        found = sset_contains(&types, type);
        sset_destroy(&types);

        if (found) {
            return class;
        }
    }
    VLOG_WARN("unknown datapath type %s", type);
    return NULL;
}

int
p4rt_type_run(const char *datapath_type)
{
    const struct p4rt_class *class;
    int error;

    datapath_type = datapath_type && datapath_type[0] ? datapath_type : "system";
    class = p4rt_class_find__(datapath_type);

    error = class->type_run ? class->type_run(datapath_type) : 0;
    if (error && error != EAGAIN) {
        VLOG_ERR_RL(&rl, "%s: type_run failed (%s)",
                    datapath_type, ovs_strerror(error));
    }

    return error;
}

//int
//p4rt_port_add(struct p4rt *p, struct netdev *netdev, ofp_port_t *ofp_portp)
//{
//    ofp_port_t ofp_port = ofp_portp ? *ofp_portp : 0xffff;  // 0xffff = OFPP_NONE
//    int error;
//
//    error = p->p4rt_class->port_add(p, netdev);
//    if (!error) {
//        VLOG_INFO("Port added successful");
//    }
//
//    return error;
//}

int p4rt_port_add(struct p4rt *p, struct netdev *netdev, ofp_port_t *ofp_portp)
{
    ofp_port_t ofp_port = ofp_portp ? *ofp_portp : 0xffff;  // 0xffff = OFPP_NONE
    int error;

    error = p->p4rt_class->port_add(p, netdev);
    if (!error) {
        VLOG_INFO("Port added successful");
    }

    return error;
}