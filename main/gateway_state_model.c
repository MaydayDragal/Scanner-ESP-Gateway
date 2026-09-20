#include "gateway_state_model.h"

void gateway_state_record_result(gateway_state_t *state,const gateway_result_t *result)
{
    state->last_result=*result;
    state->last_result.present=true;
    state->last_result.acknowledged=false;
    state->last_result.original_filename[sizeof(state->last_result.original_filename)-1]='\0';
    state->last_result.derivative_filename[sizeof(state->last_result.derivative_filename)-1]='\0';
    state->last_result.message[sizeof(state->last_result.message)-1]='\0';
}

void gateway_state_acknowledge_result(gateway_state_t *state)
{ state->last_result.acknowledged=true; }

void gateway_state_stop(gateway_state_t *state,gateway_phase_t stage,uint32_t error)
{
    state->phase=GATEWAY_STOPPED;
    state->storage_uncertain=true;
    state->stopped_stage=stage;
    state->stop_error=error;
}

void gateway_state_set_phase(gateway_state_t *state,gateway_phase_t phase)
{
    if(!state->storage_uncertain) state->phase=phase;
}
