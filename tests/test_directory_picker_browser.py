"""Desktop/mobile interactions of the shipped public directory browser."""
from pathlib import Path
from test_tool_call_groups_browser import main

if __name__ == '__main__':
    main(Path(__file__).parent/'fixtures/directory-picker-keyboard-browser.html', 'directory-browser-ui')
