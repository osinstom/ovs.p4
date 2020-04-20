#include <config.h>
#include <errno.h>


#include "dpif-netdev.h"
#include "dpif-ubpf.h"
#include "dpif-provider.h"
#include "netdev-vport.h"
#include "odp-util.h"
#include "openvswitch/shash.h"
#include "openvswitch/util.h"
#include "openvswitch/vlog.h"
#include "ovs-atomic.h"


VLOG_DEFINE_THIS_MODULE(dpif_ubpf);

/* Protects against changes to 'dp_ubpfs'. */
static struct ovs_mutex dp_ubpf_mutex = OVS_MUTEX_INITIALIZER;

/* Contains all 'struct dp_netdev's. */
static struct shash dp_ubpfs OVS_GUARDED_BY(dp_ubpf_mutex)
        = SHASH_INITIALIZER(&dp_ubpfs);

struct dp_ubpf {
    const struct dpif_class *const class;
    const char *const name;
    struct ovs_refcount ref_cnt;
    struct dp_netdev *dp_netdev;
};

/* Interface to ubpf-based datapath. */
struct dpif_ubpf {
    struct dpif dpif;
    struct dp_ubpf *dp;
};

static struct dpif_ubpf *
dpif_ubpf_cast(const struct dpif *dpif)
{
    return CONTAINER_OF(dpif, struct dpif_ubpf, dpif);
}

static int
dpif_ubpf_init(void) {
    VLOG_INFO("uBPF datapath initialized");
    return 0;
}

static int
dpif_ubpf_enumerate(struct sset *all_dps, const struct dpif_class *dpif_class)
{
    return dpif_netdev_enumerate(all_dps, dpif_class);
}

static const char *
dpif_ubpf_port_open_type(const struct dpif_class *class, const char *type)
{
    VLOG_INFO("Returning port open type");
    return dpif_netdev_port_open_type(class, type);
}

//static void
//dpif_set_from_netdev(struct dpif *dpif, )
//{
//
//}

static struct dpif *
create_dpif_ubpf(struct dp_ubpf *dp)
{
    struct dpif_ubpf *dpif;

    ovs_refcount_ref(&dp->ref_cnt);

    dpif = xmalloc(sizeof *dpif);

    dpif->dpif = *create_dpif_netdev(dp->dp_netdev);
    dpif->dp = dp;

    return &dpif->dpif;
}

static int
create_dp_ubpf(const char *name, const struct dpif_class *class,
        struct dp_ubpf **dpp)
//    OVS_REQUIRES(dp_ubpf_mutex)
{
    VLOG_INFO("Create dp ubpf");
    struct dp_ubpf *dp;

    dp = xzalloc(sizeof *dp);

    int error = create_dp_netdev(name, class, &dp->dp_netdev);
    if (error) {
        VLOG_INFO("Error creating dp netdev");
        free(dp);
        return error;
    }

    shash_add(&dp_ubpfs, name, dp);

    *CONST_CAST(const struct dpif_class **, &dp->class) = class;
    *CONST_CAST(const char **, &dp->name) = xstrdup(name);

    ovs_refcount_init(&dp->ref_cnt);

    *dpp = dp;
    return 0;
}

static int
dpif_ubpf_open(const struct dpif_class *class,
               const char *name, bool create, struct dpif **dpifp)
{
    VLOG_INFO("Opening uBPF");
    int error;
    struct dp_ubpf *dp;

    ovs_mutex_lock(&dp_ubpf_mutex);
    dp = shash_find_data(&dp_ubpfs, name);
    if (!dp) {
        error = create ? create_dp_ubpf(name, class, &dp) : ENODEV;
    } else {
        error = (dp->class != class ? EINVAL
                                    : create ? EEXIST
                                             : 0);
    }
    if (!error) {
        *dpifp = create_dpif_ubpf(dp);
    }
    ovs_mutex_unlock(&dp_ubpf_mutex);
    VLOG_INFO("uBPF datapath device opened");
    return error;
}

static void
dpif_ubpf_close(struct dpif *dpif)
{
    VLOG_INFO("Closing uBPF");
}

static int
dpif_ubpf_destroy(struct dpif *dpif)
{
    VLOG_INFO("Destroying uBPF");
    return 0;
}

static bool
dpif_ubpf_run(struct dpif *dpif)
{
    VLOG_INFO("Running uBPF");
    return true;
}

static void
dpif_ubpf_wait(struct dpif *dpif)
{
    VLOG_INFO("Waiting uBPF");
}



static int
dpif_ubpf_port_query_by_name(const struct dpif *dpif, const char *devname,
                             struct dpif_port *dpif_port)
{
    struct dp_netdev *dp = dpif_ubpf_cast(dpif)->dp->dp_netdev;
    int error;
    struct dp_netdev_port *port;

    ovs_mutex_lock(&dp->port_mutex);
    error = get_port_by_name(dp, devname, &port);
    if (!error && dpif_port) {
        answer_port_query(port, dpif_port);
    }
    ovs_mutex_unlock(&dp->port_mutex);

    return error;
}

static int
dpif_ubpf_port_dump_start(const struct dpif *dpif, void **statep)
{
    *statep = xzalloc(sizeof(struct dp_netdev_port_state));
    return 0;
}

static int
dpif_ubpf_port_dump_next(const struct dpif *dpif, void *state_,
                          struct dpif_port *dpif_port)
{
    struct dp_netdev_port_state *state = state_;
    struct dp_netdev *dp = dpif_ubpf_cast(dpif)->dp->dp_netdev;
    struct hmap_node *node;
    int retval;

    ovs_mutex_lock(&dp->port_mutex);
    node = hmap_at_position(&dp->ports, &state->position);
    if (node) {
        struct dp_netdev_port *port;

        port = CONTAINER_OF(node, struct dp_netdev_port, node);

        free(state->name);
        state->name = xstrdup(netdev_get_name(port->netdev));
        dpif_port->name = state->name;
        dpif_port->type = port->type;
        dpif_port->port_no = port->port_no;

        retval = 0;
    } else {
        retval = EOF;
    }
    ovs_mutex_unlock(&dp->port_mutex);

    return retval;
}

static int
dpif_ubpf_port_dump_done(const struct dpif *dpif, void *state_)
{
    struct dp_netdev_port_state *state = state_;
    free(state->name);
    free(state);
    return 0;
}

static int
dpif_ubpf_flow_flush(struct dpif *dpif)
{
    return 0;
}

static void
dpif_ubpf_operate(struct dpif *dpif, struct dpif_op **ops, size_t n_ops,
                  enum dpif_offload_type offload_type OVS_UNUSED)
{

}

static int
dpif_ubpf_get_stats(const struct dpif *dpif, struct dpif_dp_stats *stats)
{
    return 0;
}

static odp_port_t
ubpf_choose_port(struct dp_netdev *dp, const char *name)
    OVS_REQUIRES(dp->port_mutex)
{
    uint32_t port_no;

    for (port_no = 1; port_no <= UINT16_MAX; port_no++) {
        if (!dp_netdev_lookup_port(dp, u32_to_odp(port_no))) {
            return u32_to_odp(port_no);
        }
    }

    return ODPP_NONE;
}

static int
dpif_ubpf_port_add(struct dpif *dpif, struct netdev *netdev, odp_port_t *port_nop)
{
    struct dp_netdev *dp = dpif_ubpf_cast(dpif)->dp->dp_netdev;
    const char *dpif_port;
    char namebuf[NETDEV_VPORT_NAME_BUFSIZE];
    odp_port_t port_no;
    int error;

    dpif_port = netdev_vport_get_dpif_port(netdev, namebuf, sizeof namebuf);
    const char *type = netdev_get_type(netdev);


    ovs_mutex_lock(&dp->port_mutex);
    dpif_port = netdev_vport_get_dpif_port(netdev, namebuf, sizeof namebuf);
    if (*port_nop != ODPP_NONE) {
        port_no = *port_nop;
        error = dp_netdev_lookup_port(dp, *port_nop) ? EBUSY : 0;
    } else {
        port_no = ubpf_choose_port(dp, dpif_port);
        error = port_no == ODPP_NONE ? EFBIG : 0;
    }
    VLOG_INFO("Adding port %s %s, port_no=%d", dpif_port, type, port_no);
    if (!error) {
        *port_nop = port_no;
        error = do_add_port(dp, dpif_port, netdev_get_type(netdev), port_no);
    }
    ovs_mutex_unlock(&dp->port_mutex);

    return error;
}

static int
dpif_ubpf_port_del(struct dpif *dpif, odp_port_t port_no)
{
    struct dp_netdev *dp = dpif_ubpf_cast(dpif)->dp->dp_netdev;
    int error;

    ovs_mutex_lock(&dp->port_mutex);
    if (port_no == ODPP_LOCAL) {
        error = EINVAL;
    } else {
        struct dp_netdev_port *port;

        error = get_port_by_number(dp, port_no, &port);
        if (!error) {
            do_del_port(dp, port);
        }
    }
    ovs_mutex_unlock(&dp->port_mutex);

    return error;
}

static struct dpif_flow_dump *
dpif_ubpf_flow_dump_create(const struct dpif *dpif_, bool terse,
                           struct dpif_flow_dump_types *types OVS_UNUSED)
{

}



const struct dpif_class dpif_ubpf_class = {
        "ubpf",
        true,
        dpif_ubpf_init,
        dpif_ubpf_enumerate,
        dpif_ubpf_port_open_type,
        dpif_ubpf_open,
        dpif_ubpf_close,
        dpif_ubpf_destroy,
        dpif_ubpf_run,
        dpif_ubpf_wait,
        dpif_ubpf_get_stats,
        NULL,                      /* set_features */
        dpif_ubpf_port_add,
        dpif_ubpf_port_del,
        NULL,
        NULL,
        dpif_ubpf_port_query_by_name,
        NULL,                       /* port_get_pid */
        dpif_ubpf_port_dump_start,
        dpif_ubpf_port_dump_next,
        dpif_ubpf_port_dump_done,
        NULL,
        NULL,
        dpif_ubpf_flow_flush,
        dpif_ubpf_flow_dump_create,
        NULL,
        NULL,
        NULL,
        NULL,
        dpif_ubpf_operate,
        NULL,                       /* recv_set */
        NULL,                       /* handlers_set */
        NULL,
        NULL,
        NULL,                       /* recv */
        NULL,                       /* recv_wait */
        NULL,                       /* recv_purge */
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,                       /* ct_set_timeout_policy */
        NULL,                       /* ct_get_timeout_policy */
        NULL,                       /* ct_del_timeout_policy */
        NULL,                       /* ct_timeout_policy_dump_start */
        NULL,                       /* ct_timeout_policy_dump_next */
        NULL,                       /* ct_timeout_policy_dump_done */
        NULL,                       /* ct_get_timeout_policy_name */
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
};

void
dpif_ubpf_register()  //TODO: to remove
{
    VLOG_INFO("Registering uBPF datapath type");
//    struct dpif_class *class;
//    class = xmalloc(sizeof *class);
//    *class = dpif_ubpf_class;
//    class->type = xstrdup("ubpf");
//    dp_register_provider(class);
    VLOG_INFO("uBPF datapath type registered successfully");
}