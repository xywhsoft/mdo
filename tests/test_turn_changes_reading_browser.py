"""Quiet read recovery/publication handoff without inventing earlier changes."""
from test_tool_call_groups_browser import ROOT,main
if __name__=='__main__':
    main(ROOT/'tests/fixtures/turn-changes-reading-browser.html','turn-changes-reading')
