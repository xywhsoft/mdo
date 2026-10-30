"""Explicit stable identities for authored extension test fixtures."""
PREFIXES={'agents':'agent','subagents':'subagent','tools':'tool','skills':'skill','mcp':'mcp','external-tools':'external-tool','commands':'command','plugins':'plugin','knowledge':'knowledge'}
def extension_ref(kind,alias):
    return PREFIXES[kind]+':qa.'+alias.lower()
