"""Trace composer changes without changing input semantics in a packed QA copy.

Reuses the finite real HTTP/WS service fixture. Only its loopback proxy adds
an observer before boot and labels existing product assignments. Textarea
accessors, native input/composition events, draft writes and model requests
remain intact. Event evidence contains lengths/fingerprints, never raw text.
Open the printed wrapper; Cold tests delayed continuity, Normal tests boot.
Read #input-events on the wrapper before creating the printed stop file.
"""
from pathlib import Path
import re
from urllib.parse import urlsplit

import manual_service_read_qa as fixture
from test_api_runtime import request


class Proxy(fixture.Proxy):
    def send_bytes(self, raw, content_type, headers=None, status=200):
        self.send_response(status)
        for key, value in (headers or {}).items():
            if key.lower() not in ("content-length", "content-type", "connection",
                                   "transfer-encoding", "etag", "content-encoding"):
                self.send_header(key, value)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers(); self.wfile.write(raw)

    def proxy(self):
        if self.command != "GET": return super().proxy()
        path = urlsplit(self.path).path
        if path == "/__qa/input-observer.js":
            return self.send_bytes(Path(__file__).with_name("fixtures").joinpath(
                "packed-input-observer.js").read_bytes(), "text/javascript; charset=utf-8")
        if path == "/__qa/service":
            text = Path(__file__).with_name("packed-service-read-browser.html").read_text(encoding="utf-8")
            text = text.replace('<button id="cold">', '<button id="normal">正常启动（输入来源记录）</button><button id="cold">', 1)
            text = text.replace('<iframe id="mdo"', '<script type="application/json" id="input-events">[]</script><iframe id="mdo"', 1)
            text = text.replace("  const route =", "  const inputDocuments = new Map();\n  const route =", 1)
            text = text.replace("    const doc = frame.contentDocument, prompt =", """    const doc = frame.contentDocument;
    const trace = doc?.querySelector('#qa-input-events');
    if (trace) {
      inputDocuments.set(doc.URL.split('#')[0], JSON.parse(trace.textContent));
      document.querySelector('#input-events').textContent = JSON.stringify([...inputDocuments]);
    }
    const prompt =""", 1)
            text = text.replace("input:prompt.value,", "inputLength:prompt.value.length,", 1)
            text = text.replace("  document.querySelector('#cold').addEventListener", """  document.querySelector('#normal').addEventListener('click', () => {
    frame.src = '/?qa_reload=' + Date.now() + '#' + route;
  });
  document.querySelector('#cold').addEventListener""", 1)
            return self.send_bytes(text.encode(), "text/html; charset=utf-8")
        if path in ("/", "/js/app.js"):
            status, headers, raw = request(self.server.native, "GET", self.path)
            assert status == 200, path
            text = raw.decode("utf-8").replace("\r\n", "\n")
            if path == "/":
                assert text.count("<head>") == 1
                text = text.replace("<head>", '<head><script src="/__qa/input-observer.js"></script>', 1)
                content_type = "text/html; charset=utf-8"
            else:
                # Label the statement, leaving its native setter unchanged.
                def label(match):
                    indent, value = match.groups()
                    return indent + 'document.dispatchEvent(new CustomEvent("qa-composer-assignment", {detail: {value: ' + value + \
                        ', stack: new Error().stack}}));\n' + match[0]
                text, count = re.subn(r'^(\s*)prompt\.value = (.*);$', label, text, flags=re.MULTILINE)
                assert count >= 3, "Composer assignments changed"
                content_type = "text/javascript; charset=utf-8"
            return self.send_bytes(text.encode(), content_type, headers)
        return super().proxy()

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = proxy


if __name__ == "__main__":
    fixture.Proxy = Proxy
    fixture.main()
