#include <config.h>
#include <errno.h>
#include "lib/dpif.h"
#include "lib/netdev-vport.h"
#include "lib/odp-util.h"
#include "p4rt.h"
#include "p4rt-dpif.h"
#include "p4rt-provider.h"
#include "openvswitch/vlog.h"
#include "openvswitch/shash.h"
#include "util.h"

VLOG_DEFINE_THIS_MODULE(p4rt_dpif);

//static const struct dpif_class *base_dpif_classes[] = {
//        &dpif_ubpf_class,
//};

struct p4port_dpif {
    struct hmap_node node;
    struct p4port up;

    odp_port_t odp_port;
};

static struct shash p4rt_dpif_classes = SHASH_INITIALIZER(&p4rt_dpif_classes);

/* All existing p4rt instances, indexed by p4rt->up.type. */
struct shash all_p4rt_dpif_backers = SHASH_INITIALIZER(&all_p4rt_dpif_backers);

/* Protects 'p4rt_dpif_classes'. */
static struct ovs_mutex p4rt_dpif_mutex = OVS_MUTEX_INITIALIZER;


static struct p4port_dpif *
p4port_dpif_cast(const struct p4port *p4port)
{
    return p4port ? CONTAINER_OF(p4port, struct p4port_dpif, up) : NULL;
}

static inline struct p4rt_dpif *
p4rt_dpif_cast(const struct p4rt *p4rt)
{
    ovs_assert(p4rt->p4rt_class == &p4rt_dpif_class);
    return CONTAINER_OF(p4rt, struct p4rt_dpif, up);
}

static void
dp_initialize(void)
{
    static struct ovsthread_once once = OVSTHREAD_ONCE_INITIALIZER;

    if (ovsthread_once_start(&once)) {
        dp_register_provider(&dpif_ubpf_class);
        ovsthread_once_done(&once);
    }
}

void
p4rt_dpif_init()
{
    VLOG_INFO("Initializing P4rt Dpif");
    dp_initialize();
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
//    VLOG_INFO("p4rt_dpif_type_run");
    return 0;
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

    hmap_init(&backer->odp_to_p4port_map);
    ovs_rwlock_init(&backer->odp_to_p4port_lock);

    *backerp = backer;

    shash_add(&all_p4rt_dpif_backers, type, backer);
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

/* ## ---------------- ## */
/* ##      START       ## */
/* ## p4port Functions ## */
/* ## ---------------- ## */

static struct p4port *
p4rt_dpif_port_alloc(void)
{
    struct p4port_dpif *port = xzalloc(sizeof *port);
    return &port->up;
}

static int
p4rt_dpif_port_construct(struct p4port *p4port_)
{
    struct p4port_dpif *port = p4port_dpif_cast(p4port_);
    struct p4rt_dpif *p4rt = p4rt_dpif_cast(port->up.p4rt);
    const struct netdev *netdev = port->up.netdev;
    char namebuf[NETDEV_VPORT_NAME_BUFSIZE];
    const char *dp_port_name;
    struct dpif_port dpif_port;
    int error;

    dp_port_name = netdev_vport_get_dpif_port(netdev, namebuf, sizeof namebuf);
    error = dpif_port_query_by_name(p4rt->backer->dpif, dp_port_name,
                                    &dpif_port);
    if (error) {
        return error;
    }

    port->odp_port = dpif_port.port_no;

    ovs_rwlock_wrlock(&p4rt->backer->odp_to_p4port_lock);
    hmap_insert(&p4rt->backer->odp_to_p4port_map, &port->node,
                hash_odp_port(port->odp_port));
    ovs_rwlock_unlock(&p4rt->backer->odp_to_p4port_lock);

    dpif_port_destroy(&dpif_port);

    return 0;
}

/* ## ---------------- ## */
/* ##      END         ## */
/* ## p4port Functions ## */
/* ## ---------------- ## */

static void
p4rt_port_from_dpif_port(struct p4rt_dpif *p4rt, struct p4rt_port *port, struct dpif_port *dpif_port)
{
    port->name = dpif_port->name;
    port->type = dpif_port->type;
    port->port_no = dpif_port->port_no;
}

static int
p4rt_dpif_port_query_by_name(const struct p4rt *p4rt_, const char *devname, struct p4rt_port *port)
{
    struct p4rt_dpif *p4rt = p4rt_dpif_cast(p4rt_);
    int error;
    struct dpif_port dpif_port;

    error = dpif_port_query_by_name(p4rt->backer->dpif,
                                    devname, &dpif_port);
    VLOG_INFO("Query dpif %s, Success? %d", devname, error);
    if (!error) {
        VLOG_INFO("Port from dpif: %s, %s", dpif_port.name, dpif_port.type);
        p4rt_port_from_dpif_port(p4rt, port, &dpif_port);
    }
    return error;
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
    p4rt_dpif_port_alloc,
    p4rt_dpif_port_construct,
    NULL,
    NULL,
    p4rt_dpif_port_query_by_name,
    p4rt_dpif_port_add,
};

