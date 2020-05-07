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

/* ## --------------------------------------- ## */
/* ## p4rt-dpif helper structures definition. ## */
/* ## --------------------------------------- ## */

struct p4port_dpif {
    struct hmap_node hmap_node;
    struct p4port up;

    odp_port_t odp_port;
};

struct program_dpif {
    struct program up;

    uint32_t id;
    // TODO: other datapath-specific fields
};

/* ## ------------------------------------------ ## */
/* ## Global (shared) objects used by p4rt-dpif. ## */
/* ## ------------------------------------------ ## */

static struct shash p4rt_dpif_classes = SHASH_INITIALIZER(&p4rt_dpif_classes);

/* All existing p4rt instances, indexed by p4rt->up.type. */
struct shash all_p4rt_dpif_backers = SHASH_INITIALIZER(&all_p4rt_dpif_backers);

/* Protects 'p4rt_dpif_classes'. */
static struct ovs_mutex p4rt_dpif_mutex = OVS_MUTEX_INITIALIZER;

/* ## --------------------------- ## */
/* ## p4rt-dpif helper functions. ## */
/* ## --------------------------- ## */

static inline struct p4port_dpif *
p4port_dpif_cast(const struct p4port *p4port)
{
    return p4port ? CONTAINER_OF(p4port, struct p4port_dpif, up) : NULL;
}

struct p4port_dpif *
ofp_port_to_p4port(const struct p4rt_dpif *p4rt, ofp_port_t ofp_port)
{
    struct p4port *p4port = p4rt_get_port(&p4rt->up, ofp_port);
    return p4port ? p4port_dpif_cast(p4port) : NULL;
}

static inline struct p4rt_dpif *
p4rt_dpif_cast(const struct p4rt *p4rt)
{
    ovs_assert(p4rt->p4rt_class == &p4rt_dpif_class);
    return CONTAINER_OF(p4rt, struct p4rt_dpif, up);
}

static inline struct program_dpif *
p4program_dpif_cast(const struct program *prog)
{
    return CONTAINER_OF(prog, struct program_dpif, up);
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
    /* Return only ubpf as a proper type for P4 datapath */
    sset_add(types, "ubpf");
}

static int
p4rt_dpif_enumerate_names(const char *type, struct sset *names)
{
    struct p4rt_dpif *p4rt;

    sset_clear(names);
    sset_add(names, "ubpf");  //FIXME: hardcoded "ubpf" as only one dpif is supported now.
    return 0;
}


static int
p4rt_dpif_del(const char *type, const char *name)
{
    struct dpif *dpif;
    int error;

    error = dpif_open(name, type, &dpif);
    if (!error) {
        error = dpif_delete(dpif);
        dpif_close(dpif);
    }
    return error;
}

static int
p4rt_dpif_type_run(const char *type)
{
    VLOG_INFO("p4rt_dpif_type_run");
    struct p4rt_dpif_backer *backer;

    backer = shash_find_data(&all_p4rt_dpif_backers, type);
    if (!backer) {
        /* This is not necessarily a problem, since backers are only
         * created on demand. */
        return 0;
    }

    dpif_run(backer->dpif);

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
    ovs_rwlock_destroy(&backer->odp_to_p4port_lock);
    hmap_destroy(&backer->odp_to_p4port_map);
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
    hmap_insert(&p4rt->backer->odp_to_p4port_map, &port->hmap_node,
                hash_odp_port(port->odp_port));
    ovs_rwlock_unlock(&p4rt->backer->odp_to_p4port_lock);

    dpif_port_destroy(&dpif_port);

    return 0;
}

static void
p4rt_dpif_port_destruct(struct p4port *port_, bool del)
{
    struct p4port_dpif *port = p4port_dpif_cast(port_);
    struct p4rt_dpif *p4rt = p4rt_dpif_cast(port->up.p4rt);
    const char *devname = netdev_get_name(port->up.netdev);
    const char *netdev_type = netdev_get_type(port->up.netdev);
    char namebuf[NETDEV_VPORT_NAME_BUFSIZE];
    const char *dp_port_name;

    dp_port_name = netdev_vport_get_dpif_port(port->up.netdev, namebuf,
                                              sizeof namebuf);
    if (del && dpif_port_exists(p4rt->backer->dpif, dp_port_name)) {
        dpif_port_del(p4rt->backer->dpif, port->odp_port, false);
    } else if (del) {
        dpif_port_del(p4rt->backer->dpif, port->odp_port, true);
    }

    if (port->odp_port != ODPP_NONE) {
        ovs_rwlock_wrlock(&p4rt->backer->odp_to_p4port_lock);
        hmap_remove(&p4rt->backer->odp_to_p4port_map, &port->hmap_node);
        ovs_rwlock_unlock(&p4rt->backer->odp_to_p4port_lock);
    }
}

static void
p4rt_dpif_port_dealloc(struct p4port *p4port)
{
    struct p4port_dpif *port = p4port_dpif_cast(p4port);
    free(port);
}

/* ## ---------------- ## */
/* ##      END         ## */
/* ## p4port Functions ## */
/* ## ---------------- ## */

static int
p4rt_dpif_run(struct p4rt *p4rt)
{
    return 0;
}

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

static int
p4rt_dpif_port_del(struct p4rt *p, ofp_port_t port_no)
{
    struct p4rt_dpif *p4rt = p4rt_dpif_cast(p);

    VLOG_INFO("Deleting port %d", port_no);
    // FIXME: there is not translation between ofp_port <-> odp_port.
    return dpif_port_del(p4rt->backer->dpif, port_no, false);
}

static struct program *
p4rt_dpif_prog_alloc()
{
    struct program_dpif *program = xzalloc(sizeof *program);
    return &program->up;
}

static int
p4rt_dpif_prog_insert(struct program *prog)
{
    int error;
    VLOG_INFO("Inserting program");
    struct p4rt_dpif *p4rt = p4rt_dpif_cast(prog->p4rt);
    struct program_dpif *prog_dpif = p4program_dpif_cast(prog);
    struct dpif *dpif = p4rt->backer->dpif;

    struct dpif_prog dpif_prog = {
            .id = 0,
            .data = prog->data,
            .data_len = prog->data_len,
    };

    error = dpif->dpif_class->dp_prog_set(dpif, dpif_prog);

    if (!error) {
        prog_dpif->id = dpif_prog.id;
    }

    return error;
}

static void
p4rt_dpif_prog_delete(struct program *prog)
{
    struct p4rt_dpif *p4rt = p4rt_dpif_cast(prog->p4rt);
    struct program_dpif *prog_dpif = p4program_dpif_cast(prog);
    struct dpif *dpif = p4rt->backer->dpif;

    dpif->dpif_class->dp_prog_unset(dpif, 0);  // FIXME: hardcoded prog id.
}

static void
p4rt_dpif_prog_dealloc(struct program *prog)
{
    struct program_dpif *prog_dpif = p4program_dpif_cast(prog);
    free(prog_dpif);
}

const struct p4rt_class p4rt_dpif_class = {
    p4rt_dpif_init, /* init */
    NULL,           /* port_open_type */
    p4rt_dpif_enumerate_types,
    p4rt_dpif_enumerate_names,
    p4rt_dpif_del,
    p4rt_dpif_type_run,
    p4rt_dpif_alloc,
    p4rt_dpif_construct,
    p4rt_dpif_destruct,
    p4rt_dpif_dealloc,
    p4rt_dpif_run,
    p4rt_dpif_port_alloc,
    p4rt_dpif_port_construct,
    p4rt_dpif_port_destruct,
    p4rt_dpif_port_dealloc,
    p4rt_dpif_port_query_by_name,
    p4rt_dpif_port_add,
    p4rt_dpif_port_del,
    p4rt_dpif_prog_alloc,
    p4rt_dpif_prog_insert,
    p4rt_dpif_prog_delete,
    p4rt_dpif_prog_dealloc,
};

