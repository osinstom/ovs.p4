#include <config.h>
#include <errno.h>
#include "lib/dpif.h"
#include "lib/netdev-vport.h"
#include "lib/odp-util.h"
#include "p4rt-dpif.h"
#include "p4rt-dpif-provider.h"
#include "p4rt-provider.h"
#include "openvswitch/vlog.h"
#include "openvswitch/shash.h"
#include "util.h"

VLOG_DEFINE_THIS_MODULE(p4rt_dpif);

static const struct dpif_class *base_dpif_classes[] = {
        &dpif_ubpf_class,
};

struct registered_dpif_class {
    const struct p4rt_dpif_class *dpif_class;
    int refcount;
};

static struct shash p4rt_dpif_classes = SHASH_INITIALIZER(&p4rt_dpif_classes);

/* All existing p4rt instances, indexed by p4rt->up.type. */
struct shash all_p4rt_dpif_backers = SHASH_INITIALIZER(&all_p4rt_dpif_backers);

/* Protects 'p4rt_dpif_classes'. */
static struct ovs_mutex p4rt_dpif_mutex = OVS_MUTEX_INITIALIZER;


static inline struct p4rt_dpif *
p4rt_dpif_cast(const struct p4rt *p4rt)
{
    ovs_assert(p4rt->p4rt_class == &p4rt_dpif_class);
    return CONTAINER_OF(p4rt, struct p4rt_dpif, up);
}

static struct registered_dpif_class *
dp_class_lookup(const char *type)
{
    VLOG_INFO("Type: %s", type);
    struct registered_dpif_class *rc;
    ovs_mutex_lock(&p4rt_dpif_mutex);
    rc = shash_find_data(&p4rt_dpif_classes, type);
    if (rc) {
        rc->refcount++;
    }
    ovs_mutex_unlock(&p4rt_dpif_mutex);
    return rc;
}

static void
dp_initialize(void)
{
    static struct ovsthread_once once = OVSTHREAD_ONCE_INITIALIZER;

    if (ovsthread_once_start(&once)) {
        int i;

        for (i = 0; i < ARRAY_SIZE(base_dpif_classes); i++) {
            dp_register_provider(base_dpif_classes[i]);
        }

        ovsthread_once_done(&once);
    }
}

void
p4rt_dpif_init()
{
    VLOG_INFO("Initializing P4rt Dpif");
}

const char *
p4rt_dpif_port_open_type(const char *datapath_type, const char *port_type)
{

}

static void
p4rt_dpif_enumerate_types(struct sset *types)
{
    dp_enumerate_types(types);
}

static int
p4rt_dpif_type_run(const char *type)
{
    VLOG_INFO("p4rt_dpif_type_run");
}

static struct p4rt *
p4rt_dpif_alloc()
{
    struct p4rt_dpif *p4rt = xzalloc(sizeof *p4rt);
    return &p4rt->up;
}

static void
p4rt_dpif_dealloc(struct p4rt *p4rt_)
{
    struct p4rt_dpif *p4rt = p4rt_dpif_cast(p4rt_);
    free(p4rt);
}

static int
open_p4rt_dpif_backer(const char *type, struct p4rt_dpif_backer **backerp)
{
    int error;
    struct p4rt_dpif_backer *backer;
    char *backer_name;
    backer_name = xasprintf("ovs-%s", type);

    backer = xmalloc(sizeof *backer);

    error = dpif_create_and_open(backer_name, type, &backer->dpif);
    free(backer_name);
    if (error) {
        VLOG_ERR("failed to open datapath of type %s: %s", type,
                 ovs_strerror(error));
        free(backer);
        return error;
    }

    backer->type = xstrdup(type);

    shash_add(&all_p4rt_dpif_backers, type, backer);

    *backerp = backer;
    return error;
}

static int
p4rt_dpif_construct(struct p4rt *p4rt_)
{
    VLOG_INFO("Constructing");
    int error;
    struct p4rt_dpif *p4rt = p4rt_dpif_cast(p4rt_);

    error = open_p4rt_dpif_backer(p4rt->up.type, &p4rt->backer);
    if (error) {
        return error;
    }

    uuid_generate(&p4rt->uuid);


    return error;
}

static int
p4rt_dpif_delete(struct p4rt_dpif *dpif)
{
    return 0;
}

static void
close_p4rt_dpif_backer(struct p4rt_dpif_backer *backer, bool del)
{
//    ovs_assert(backer->refcount > 0);
//
//    if (--backer->refcount) {
//        return;
//    }

    shash_find_and_delete(&all_p4rt_dpif_backers, backer->type);
    free(backer->type);

    if (del) {
        dpif_delete(backer->dpif);
    }
    dpif_close(backer->dpif);

    free(backer);
}

static void
p4rt_dpif_destruct(struct p4rt *p4rt_, bool del)
{
    struct p4rt_dpif *p4rt = p4rt_dpif_cast(p4rt_);
    close_p4rt_dpif_backer(p4rt->backer, del);
}

static int
p4rt_dpif_port_add(struct p4rt *p, struct netdev *netdev)
{
    struct p4rt_dpif *p4rt = p4rt_dpif_cast(p);
    const char *devname = netdev_get_name(netdev);
    char namebuf[NETDEV_VPORT_NAME_BUFSIZE];
    const char *dp_port_name;

    dp_port_name = netdev_vport_get_dpif_port(netdev, namebuf, sizeof namebuf);
    if (!dpif_port_exists(p4rt->backer->dpif, dp_port_name)) {
        VLOG_INFO("Port does not exist!");
        odp_port_t port_no = ODPP_NONE;
        int error;

        error = dpif_port_add(p4rt->backer->dpif, netdev, &port_no);
        if (error) {
            return error;
        }
    }
    VLOG_INFO("Port exists!");

    return 0;
}

const struct p4rt_class p4rt_dpif_class = {
    p4rt_dpif_init, /* init */
    NULL,           /* port_open_type */
    p4rt_dpif_enumerate_types,
    p4rt_dpif_type_run,
    p4rt_dpif_alloc,
    p4rt_dpif_construct,
    p4rt_dpif_destruct,
    p4rt_dpif_dealloc,
    p4rt_dpif_port_add,
};

