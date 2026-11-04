"""Read one browser fixture's JSON result from a --dump-dom document."""
from html.parser import HTMLParser


class ResultParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.result = ''
        self._result_tag = None

    def handle_starttag(self, tag, attrs):
        if self._result_tag is None and not self.result and dict(attrs).get('id') == 'result':
            self._result_tag = tag

    def handle_endtag(self, tag):
        if tag == self._result_tag:
            self._result_tag = None

    def handle_data(self, data):
        if self._result_tag is not None:
            self.result += data
