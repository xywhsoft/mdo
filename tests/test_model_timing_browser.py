"""Model request timings are rendered only for the runtime's explicit debug flag."""
from test_tool_call_groups_browser import ROOT, main

if __name__ == '__main__':
    main(ROOT / 'tests/fixtures/model-timing-browser.html', 'model-timing-debug')
