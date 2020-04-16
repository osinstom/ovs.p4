#ifndef P4RT_DPIF_H
#define P4RT_DPIF_H

#include "p4rt-provider.h"
#include "lib/uuid.h"

/* All datapaths of a given type share a single dpif backer instance. */
struct p4rt_dpif_backer {
    char *type;
    struct p4rt_dpif *dpif;
};

struct p4rt_dpif {
    struct p4rt up;
    struct p4rt_dpif_backer *backer;

    /* Unique identifier for this instantiation of this bridge in this running
     * process.  */
    struct uuid uuid;
};


#endif //P4RT_DPIF_H
