#include "native.h"

/*
 * CUSTOM RESEARCH IMPLEMENTATION POINTS
 * -------------------------------------
 * These deliberately contain no privileged operations or crypto service code.
 * The loader and diagnostic transport do not depend on any implementation here.
 */
static int research_prepare(void)
{
    /* Add ordinary initialization here, if needed. */
    return 0;
}

static int research_execute(void)
{
    /* Deliberately unimplemented. */
    return RESEARCH_STAGE_UNIMPLEMENTED;
}

static void research_finish(void)
{
    /* Deliberately empty. */
}

int research_stage_run(void)
{
    int status = research_prepare();
    if (!status) status = research_execute();
    research_finish();
    return status;
}
