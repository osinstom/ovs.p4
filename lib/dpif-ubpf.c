#include <config.h>
#include <errno.h>
#include "dpif-ubpf.h"

#include "dpif-netdev.h"
#include "dpif-provider.h"
#include "ovs-atomic.h"
#include "openvswitch/shash.h"
#include "openvswitch/vlog.h"

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
};

/* Interface to ubpf-based datapath. */
struct dpif_ubpf {
    struct dpif dpif;
    struct dp_ubpf *dp;
};

static int
dpif_ubpf_init(void) {
    VLOG_INFO("uBPF datapath initialized");
    return 0;
}

static const char *
dpif_ubpf_port_open_type(const struct dpif_class *class, const char *type)
{
    VLOG_INFO("Returning port open type");
    return "tap";
}

static struct dpif *
create_dpif_ubpf(struct dp_ubpf *dp)
{
    uint16_t netflow_id = hash_string(dp->name, 0);
    struct dpif_ubpf *dpif;

    ovs_refcount_ref(&dp->ref_cnt);

    dpif = xmalloc(sizeof *dpif);
    dpif_init(&dpif->dpif, dp->class, dp->name, netflow_id >> 8, netflow_id);
    dpif->dp = dp;

    return &dpif->dpif;
}

static int
create_dp_ubpf(const char *name, const struct dpif_class *class,
        struct dp_ubpf **dpp)
    OVS_REQUIRES(dp_ubpf_mutex)
{
    struct dp_ubpf *dp;

    dp = xzalloc(sizeof *dp);
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
dpif_ubpf_port_dump_start(const struct dpif *dpif, void **statep)
{
    return 0;
}

static int
dpif_ubpf_port_dump_next(const struct dpif *dpif, void *state,
                          struct dpif_port *port)
{
    return EOF;
}

static int
dpif_ubpf_port_dump_done(const struct dpif *dpif, void *state)
{
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

static struct dpif_flow_dump *
dpif_ubpf_flow_dump_create(const struct dpif *dpif_, bool terse,
                           struct dpif_flow_dump_types *types OVS_UNUSED)
{

}

const struct dpif_class dpif_ubpf_class = {
        "ubpf",
        true,
        dpif_ubpf_init,
        NULL,
        dpif_ubpf_port_open_type,
        dpif_ubpf_open,
        dpif_ubpf_close,
        dpif_ubpf_destroy,
        dpif_ubpf_run,
        dpif_ubpf_wait,
        dpif_ubpf_get_stats,
        NULL,                      /* set_features */
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
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
dpif_ubpf_register()
{
    VLOG_INFO("Registering uBPF datapath type");
    struct dpif_class *class;
    class = xmalloc(sizeof *class);
    *class = dpif_ubpf_class;
    class->type = xstrdup("ubpf");
    dp_register_provider(class);
    VLOG_INFO("uBPF datapath type registered successfully");
}