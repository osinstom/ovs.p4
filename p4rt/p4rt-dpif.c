#include <config.h>
#include <errno.h>
#include "p4rt-dpif.h"
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

/* All existing p4rt instances, indexed by p4rt->up.type. */
struct shash all_p4rt_dpif_backers = SHASH_INITIALIZER(&all_p4rt_dpif_backers);

static struct shash p4rt_dpif_classes = SHASH_INITIALIZER(&p4rt_dpif_classes);

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

static int
do_open(const char *name, const char *type, bool create, struct p4rt_dpif **dpifp)
{
    struct p4rt_dpif *dpif = NULL;
    int error;
    struct registered_dpif_class *registered_class;

    dp_initialize();

    type = dpif_normalize_type(type);
    registered_class = dp_class_lookup(type);
    if (!registered_class) {
        VLOG_WARN("could not create datapath %s of unknown type %s", name,
                  type);
        error = EAFNOSUPPORT;
        goto exit;
    }

    error = registered_class->dpif_class->open(registered_class->dpif_class, name, create, &dpif);
    if (!error) {

    } else {

    }

exit:
    *dpifp = error ? NULL : dpif;
    return error;
}

int
p4rt_dpif_create(const char *name, const char *type, struct p4rt_dpif **dpifp)
{
    return do_open(name, type, true, dpifp);
}

static int
dpif_create_and_open(const char *name, const char *type, struct p4rt_dpif **dpifp)
{
    int error;
    error = p4rt_dpif_create(name, type, dpifp);
    if (error == EEXIST || error == EBUSY) {
        error = dpif_open(name, type, dpifp);
        if (error) {
            VLOG_WARN("datapath %s already exists but cannot be opened: %s",
                      name, ovs_strerror(error));
        }
    } else if (error) {
        VLOG_WARN("failed to create datapath %s: %s",
                  name, ovs_strerror(error));
    }
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
    struct p4rt_dpif *p4rt = p4rt_dpif_cast(p4rt_);



    uuid_generate(&p4rt->uuid);


    return 0;
}

static int
p4rt_dpif_port_add(struct p4rt *p, struct netdev *netdev)
{
    int error;
    struct p4rt_dpif *p4rt = p4rt_dpif_cast(p);
    const char *devname = netdev_get_name(netdev);

//    error = dpif_port_add(p4rt->backer->dpif, netdev, &port_no);

    return error;
}

const struct p4rt_class p4rt_dpif_class = {
    p4rt_dpif_init, /* init */
    NULL,           /* port_open_type */
    p4rt_dpif_enumerate_types,
    p4rt_dpif_type_run,
    NULL,
    p4rt_dpif_construct,
    NULL,
    NULL,
    p4rt_dpif_port_add,
};

