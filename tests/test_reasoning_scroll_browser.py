"""Real scrolling through streamed reasoning, DOM replacement and cached sessions."""
from test_tool_call_groups_browser import ROOT,main
if __name__=='__main__':
    main(ROOT/'tests/fixtures/reasoning-scroll-browser.html','reasoning-scroll',('zh-CN',))
