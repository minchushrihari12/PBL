"""server.py - tiny web server (Python standard library only).
Serves the website from web/ and answers GET /run by calling ./mqclient,
which talks to the core process through message queues."""
import json, os, subprocess
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

BASE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.join(BASE, "web")

def err(msg):
    return json.dumps({"ok": False, "error": msg}).encode()

class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=WEB, **k)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path != "/run":
            return super().do_GET()
        q = parse_qs(url.query)
        refs = q.get("refs", [""])[0][:1000]
        try:
            frames = int(q.get("frames", ["0"])[0])
            algo = int(q.get("algo", ["0"])[0])
        except ValueError:
            body = err("Frames and algorithm must be numbers.")
        else:
            try:
                out = subprocess.run([os.path.join(BASE, "mqclient"), refs, str(frames), str(algo)],
                                     capture_output=True, timeout=10, cwd=BASE)
                body = out.stdout or err("Empty reply from core.")
            except subprocess.TimeoutExpired:
                body = err("Core did not answer in time.")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

if __name__ == "__main__":
    print("server: open http://localhost:8000")
    ThreadingHTTPServer(("0.0.0.0", 8000), Handler).serve_forever()
