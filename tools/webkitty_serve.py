#!/usr/bin/env python3
"""Serve the WebKitty folder exactly as-is, with one addition on the wire only:

  every *.manifest is sent with an extra comment line  "# directboot payload <sha256>"
  (sha of the file in the GoldHEN slot) and never as 304.

Why: the PS4 browser's appcache only re-downloads cached files when the manifest bytes change.
The GoldHEN .bin files are in the manifests' CACHE section, so a new payload is otherwise never
fetched (HW run 19:26 UTC ran the 19:23 cached build). The files on disk are not modified.
"""
import hashlib, http.server, os, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "/site"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8000
SLOT = os.path.join(ROOT, "includes/payloads/HEN/goldhen_v2.4b18.14.bin")


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = dict(http.server.SimpleHTTPRequestHandler.extensions_map,
                          **{".manifest": "text/cache-manifest", ".mjs": "text/javascript"})

    def __init__(self, *a, **kw):
        super().__init__(*a, directory=ROOT, **kw)

    def do_GET(self):
        path = self.translate_path(self.path)
        if path.endswith(".manifest") and os.path.isfile(path):
            try:
                tag = hashlib.sha256(open(SLOT, "rb").read()).hexdigest()
            except OSError:
                tag = "none"
            body = open(path, "rb").read().rstrip(b"\n") + b"\n# directboot payload " + tag.encode() + b"\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/cache-manifest")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            self.wfile.write(body)
            return
        return super().do_GET()


http.server.ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
