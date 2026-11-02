#ifndef MDO_AGENT_LIMITS_H
#define MDO_AGENT_LIMITS_H

/* Global and role ceilings both use zero for unlimited. */
static inline uint32 MdoAgentEffectiveTurnLimit(uint32 Global, uint32 Role)
{
    return !Global ? Role : !Role || Global < Role ? Global : Role;
}

#endif
