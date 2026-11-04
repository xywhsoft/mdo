"""Recovery decisions must not compete with a managed steering handoff."""
from test_tool_call_groups_browser import ROOT,main
if __name__=='__main__':
    main(ROOT/'tests/fixtures/steering-recovery-browser.html','steering-recovery')
