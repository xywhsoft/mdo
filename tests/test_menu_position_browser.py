"""Real-DOM menu anchors after sidebar rebuilds and at viewport edges."""
from test_tool_call_groups_browser import ROOT, main

if __name__ == '__main__':
    main(ROOT / 'tests/fixtures/menu-position-browser.html', 'menu-position')
