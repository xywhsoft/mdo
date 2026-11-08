// Derived service-restart status is not a tool result or a new journal event.
export function interruptedToolNotice(event) {
  const state=event.tool_outcome;
  return event.kind==='tool_start' && event.run_id===0 && !(event.agent_depth>0) &&
    /^sdk-[0-9a-f]{32}$/.test(event.tool_call_id||'') &&
    state?.state==='interrupted' && state.code==='sdk_operation_interrupted' &&
    state.may_have_completed===true && typeof state.message==='string' &&
    state.message.length>0 && state.message.length<=512 ? state.message : '';
}
