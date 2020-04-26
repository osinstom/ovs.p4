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
#include "bpf.h"

VLOG_DEFINE_THIS_MODULE(dpif_ubpf);

/* Protects against changes to 'dp_ubpfs'. */
static struct ovs_mutex dp_ubpf_mutex = OVS_MUTEX_INITIALIZER;

/* Contains all 'struct dp_netdev's. */
static struct shash dp_ubpfs OVS_GUARDED_BY(dp_ubpf_mutex)
        = SHASH_INITIALIZER(&dp_ubpfs);

struct dp_prog {
    uint16_t id;
    struct ubpf_vm *vm;
};

struct dp_netdev_action_flow {
    struct cmap_node node;
    uint32_t hash;
    struct nlattr *action;

    struct packet_batch_per_action *action_batch;
};

struct packet_batch_per_action {
    struct dp_packet_batch output_batch;
    struct dp_netdev_action_flow *action;
};

struct dp_ubpf {
    struct dp_netdev *dp_netdev;
    const char *const name;

    /* Data plane program. */
    struct dp_prog *prog;
};

static struct dp_ubpf *
dp_ubpf_cast(struct dp_netdev *dp_netdev)
{
    return CONTAINER_OF(dp_netdev, struct dp_ubpf, dp_netdev);
}

/* Interface to ubpf-based datapath. */
struct dpif_ubpf {
    struct dpif_netdev dpif_netdev;
    struct dp_ubpf *dp;
};

static struct dpif_ubpf *
dpif_ubpf_cast(const struct dpif *dpif)
{
    return CONTAINER_OF(dpif, struct dpif_ubpf, dpif_netdev.dpif);
}

static inline struct dp_netdev_action_flow *
dp_netdev_action_flow_init(struct dp_netdev_pmd_thread *pmd,
                           uint16_t action_type,
                           void *actions_args,
                           uint32_t hash)
{
    struct dp_netdev_action_flow *act_flow = xmalloc(sizeof *act_flow);  // TODO: must be freed somewhere later
    struct nlattr *act;
    switch (action_type) {
        case REDIRECT: {
            uint32_t port = *((uint32_t *)actions_args);
            act = xzalloc(NLA_HDRLEN + sizeof(port));
            act->nla_len = NLA_HDRLEN + sizeof(port);
            act->nla_type = OVS_ACTION_ATTR_OUTPUT;
            nullable_memcpy(act + 1, &port, sizeof(port));
            break;
        }
    }
    act_flow->action = act;
    act_flow->action_batch = NULL; // force batch initialization
    act_flow->hash = hash;

    cmap_insert(&pmd->action_table, &act_flow->node, hash);

    return act_flow;
}

static inline struct dp_netdev_action_flow *
get_dp_netdev_action_flow(struct dp_netdev_pmd_thread *pmd,
                          uint32_t hash)
{
    struct cmap_node *node;
    struct dp_netdev_action_flow *act_flow;

    node = cmap_find(&pmd->action_table, hash);
    if (OVS_LIKELY(node != NULL)) {
        return CONTAINER_OF(node, struct dp_netdev_action_flow, node);
    }
    return NULL;
}

static inline void
packet_batch_per_action_init(struct packet_batch_per_action *batch,
                             struct dp_netdev_action_flow *action)
{
    action->action_batch = batch;
    batch->action = action;

    dp_packet_batch_init(&batch->output_batch);
}

static inline void
packet_batch_per_action_update(struct packet_batch_per_action *batch,
                               struct dp_packet *pkt)
{
    dp_packet_batch_add(&batch->output_batch, pkt);
}

static inline void
dp_netdev_queue_action_batches(struct dp_packet *pkt,
                               struct dp_netdev_action_flow *action)
{
    struct packet_batch_per_action *batch = action->action_batch;

    if (OVS_UNLIKELY(!batch)) {
        batch = xmalloc(sizeof(struct packet_batch_per_action));
        packet_batch_per_action_init(batch, action);
    }

    packet_batch_per_action_update(batch, pkt);
}

static inline void
packet_batch_per_action_execute(struct packet_batch_per_action *batch,
                                struct dp_netdev_pmd_thread *pmd)
{
    struct nlattr *act = batch->action->action;
    dp_netdev_execute_actions(pmd, &batch->output_batch, false, NULL,
                              act, NLA_HDRLEN + sizeof(uint32_t)); // FIXME: this is really temporary
    dp_packet_batch_init(&batch->output_batch);
}

static inline void
protocol_independent_processing(struct dp_netdev_pmd_thread *pmd,
                                struct dp_packet_batch *packets_,
                                odp_port_t in_port)
{
    struct dp_ubpf *dp = dp_ubpf_cast(pmd->dp);

    if (OVS_LIKELY(dp->prog)) {
        struct dp_packet *packet;
        DP_PACKET_BATCH_FOR_EACH (i, packet, packets_) {
                struct standard_metadata std_meta = {
                        .input_port = odp_to_u32(in_port),
                };

                ubpf_handle_packet(dp->prog->vm, &std_meta, packet);
//            VLOG_INFO("From uBPF, action = %d", std_meta.output_action);
                switch (std_meta.output_action) {
                    case REDIRECT: {
//                    VLOG_INFO("Action Redirect, port = %d", std_meta.output_port);
                        uint32_t hash = hash_2words(std_meta.output_action,
                                                    std_meta.output_port);

                        struct dp_netdev_action_flow *act_flow;
                        act_flow = get_dp_netdev_action_flow(pmd, hash);
                        if (OVS_UNLIKELY(!act_flow)) {
                            act_flow = dp_netdev_action_flow_init(pmd,
                                                                  REDIRECT, &std_meta.output_port, hash);
                        }
                        dp_netdev_queue_action_batches(packet, act_flow);
                        break;
                    }
                }
            }

        struct dp_netdev_action_flow *output_flow;
        CMAP_FOR_EACH(output_flow, node, &pmd->action_table) {
            packet_batch_per_action_execute(output_flow->action_batch, pmd);
        }

    }

}

static void
process_ubpf(struct dp_netdev_pmd_thread *pmd,
             struct dp_packet_batch *packets,
             bool md_is_valid, odp_port_t port_no)
{
    VLOG_INFO("uBPF processing");
    protocol_independent_processing(pmd, packets, port_no);
}

static int
dpif_ubpf_init(void)
{
    // initialize dpif-netdev too.
    dpif_netdev_init();
    VLOG_INFO("uBPF datapath initialized");
    return 0;
}

static struct dpif *
create_dpif_ubpf(struct dp_ubpf *dp)
{
    struct dpif_ubpf *dpif;
    struct dp_netdev *dp_netdev;
//    ovs_refcount_ref(&dp->ref_cnt);

    dpif = xmalloc(sizeof *dpif);

    struct dpif *dpifp = create_dpif_netdev(dp->dp_netdev);
    dpif->dp = dp;
    dpif->dpif_netdev.dp = dp->dp_netdev;
    dpif->dpif_netdev.dpif = *dpifp;

    return &dpif->dpif_netdev.dpif;
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

    dp->dp_netdev->process_cb = process_ubpf;

    shash_add(&dp_ubpfs, name, dp);

//    *CONST_CAST(const struct dpif_class **, &dp->class) = class;
    *CONST_CAST(const char **, &dp->name) = xstrdup(name);
    dp->prog = NULL;

//    ovs_refcount_init(&dp->ref_cnt);

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
        error = (dp->dp_netdev->class != class ? EINVAL
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
    struct dp_netdev *dp = ((struct dpif_netdev *) dpif_ubpf_cast(dpif)->dp)->dp;
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
    VLOG_INFO("Port dump start");
    *statep = xzalloc(sizeof(struct dp_netdev_port_state));
    return 0;
}

static int
dpif_ubpf_port_dump_next(const struct dpif *dpif, void *state_,
                          struct dpif_port *dpif_port)
{
    VLOG_INFO("Port dump next");

    struct dp_netdev_port_state *state = state_;
    struct dp_netdev *dp = ((struct dpif_netdev *) dpif_ubpf_cast(dpif)->dp)->dp;
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
    struct dp_netdev *dp = (dpif_ubpf_cast(dpif)->dpif_netdev).dp;
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
    struct dp_netdev *dp = (dpif_ubpf_cast(dpif)->dpif_netdev).dp;
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

static int
dp_prog_set(struct dpif *dpif, struct dpif_prog *prog)
{
    struct dp_ubpf *dp = dpif_ubpf_cast(dpif)->dp;
    struct dp_prog *dp_prog;
    VLOG_INFO("dpif_netdev_dp_prog_set in dpif-netdev.c");
    VLOG_INFO("Injecting BPF program ID=%d", prog->id);

    struct ubpf_vm *vm = create_ubpf_vm(prog->id);
    if (!load_bpf_prog(vm, prog->data_len, prog->data)) {
        ubpf_destroy(vm);
        return -1; // FIXME: not sure what to return
    }

    dp_prog = xzalloc(sizeof *dp_prog);
    dp_prog->id = prog->id;
    dp_prog->vm = vm;

    if (dp->prog) {
        free(dp->prog);
        dp->prog = NULL;
    }
    dp->prog = dp_prog;
    VLOG_INFO("Injected");
    return 0;
}

const struct dpif_class dpif_ubpf_class = {
        "ubpf",
        true,
        dpif_ubpf_init,
        dpif_netdev_enumerate,
        dpif_netdev_port_open_type,
        dpif_ubpf_open,
        dpif_ubpf_close,
        dpif_ubpf_destroy,
        dpif_netdev_run,
        dpif_ubpf_wait,
        dpif_ubpf_get_stats,
        NULL,                      /* set_features */
        dpif_ubpf_port_add,
        dpif_ubpf_port_del,
        NULL,
        NULL,
        dpif_netdev_port_query_by_name,
//        dpif_ubpf_port_query_by_name,
        NULL,                       /* port_get_pid */
        dpif_ubpf_port_dump_start,
        dpif_netdev_port_dump_next,
//        dpif_ubpf_port_dump_next,
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
        dp_prog_set,
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