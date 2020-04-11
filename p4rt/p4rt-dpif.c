#include <config.h>
#include <errno.h>
#include "p4rt-dpif-provider.h"
#include "p4rt-provider.h"
#include "openvswitch/vlog.h"
#include "openvswitch/shash.h"
#include "util.h"

VLOG_DEFINE_THIS_MODULE(p4rt_dpif);

static const struct p4rt_dpif_class *base_dpif_classes[] = {
        &dpif_ubpf_class,
};

struct registered_dpif_class {
    const struct p4rt_dpif_class *dpif_class;
    int refcount;
};

static struct shash p4rt_dpif_classes = SHASH_INITIALIZER(&p4rt_dpif_classes);

/* Protects 'p4rt_dpif_classes'. */
static struct ovs_mutex p4rt_dpif_mutex = OVS_MUTEX_INITIALIZER;

static void
dp_initialize(void)
{
    static struct ovsthread_once once = OVSTHREAD_ONCE_INITIALIZER;

    if (ovsthread_once_start(&once)) {
        int i;

        for (i = 0; i < ARRAY_SIZE(base_dpif_classes); i++) {
            p4_dp_register_provider(base_dpif_classes[i]);
        }

        ovsthread_once_done(&once);
    }
}

static int
p4_dp_register_provider__(const struct p4rt_dpif_class *new_class)
{
    struct registered_dpif_class *registered_class;
    int error;

    if (shash_find(&p4rt_dpif_classes, new_class->type)) {
        VLOG_WARN("attempted to register duplicate datapath provider: %s",
                  new_class->type);
        return EEXIST;
    }

    error = new_class->init ? new_class->init() : 0;
    if (error) {
        VLOG_WARN("failed to initialize %s datapath class: %s",
                  new_class->type, ovs_strerror(error));
        return error;
    }

    registered_class = xmalloc(sizeof *registered_class);
    registered_class->dpif_class = new_class;
    registered_class->refcount = 0;

    shash_add(&p4rt_dpif_classes, new_class->type, registered_class);

    return 0;
}

/* Registers a new datapath provider.  After successful registration, new
 * datapaths of that type can be opened using dpif_open(). */
int
p4_dp_register_provider(const struct p4rt_dpif_class *new_class)
{
    int error;

    ovs_mutex_lock(&p4rt_dpif_mutex);
    error = p4_dp_register_provider__(new_class);
    ovs_mutex_unlock(&p4rt_dpif_mutex);

    return error;
}

void
p4rt_dpif_init()
{
    VLOG_INFO("Initializing P4rt Dpif");
}

void
p4rt_dpif_enumerate_types(struct sset *types)
{
    struct shash_node *node;

    dp_initialize();

    ovs_mutex_lock(&p4rt_dpif_mutex);
    SHASH_FOR_EACH(node, &p4rt_dpif_classes) {
        const struct registered_dpif_class *registered_class = node->data;
        VLOG_INFO("Adding P4 dp type: %s", registered_class->dpif_class->type);
        sset_add(types, registered_class->dpif_class->type);
    }
    ovs_mutex_unlock(&p4rt_dpif_mutex);
}

static int
p4rt_dpif_type_run(const char *type)
{
    VLOG_INFO("p4rt_dpif_type_run");
}

const struct p4rt_class p4rt_dpif_class = {
    p4rt_dpif_init, /* init */
    NULL,           /* port_open_type */
    p4rt_dpif_enumerate_types,
    p4rt_dpif_type_run,
};

