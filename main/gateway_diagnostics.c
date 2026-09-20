#include "gateway_diagnostics.h"
void gateway_diagnostics_record(gateway_diagnostics_t *ring,int64_t time,gateway_phase_t phase,gateway_event_t event,uint32_t error,uint32_t value1,uint32_t value2)
{
    ring->records[ring->next]=(gateway_diagnostic_record_t){time,(uint32_t)phase,(uint32_t)event,error,value1,value2};
    ring->next=(ring->next+1)%GATEWAY_DIAGNOSTIC_CAPACITY;
    if(ring->count<GATEWAY_DIAGNOSTIC_CAPACITY) ring->count++;
}
size_t gateway_diagnostics_count(const gateway_diagnostics_t *ring) {return ring->count;}
bool gateway_diagnostics_get(const gateway_diagnostics_t *ring,size_t index,gateway_diagnostic_record_t *out)
{
    if(index>=ring->count || !out) return false;
    *out=ring->records[(ring->next+GATEWAY_DIAGNOSTIC_CAPACITY-ring->count+index)%GATEWAY_DIAGNOSTIC_CAPACITY];
    return true;
}
