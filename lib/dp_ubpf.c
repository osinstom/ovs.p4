#include <config.h>
#include "dp_ubpf.h"

void
ubpf_enable()
{
    dpif_ubpf_register();
}
