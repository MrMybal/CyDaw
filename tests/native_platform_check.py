import http.server
import pathlib
import subprocess
import sys
import tempfile
import threading
import zipfile

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass
    def do_GET(self):
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/data")
            self.end_headers()
            return
        data = b"native-download"
        status = 200 if self.path == "/data" else 404
        if self.headers.get("Range") == "bytes=7-":
            status, data = 206, data[7:]
        self.send_response(status)
        self.send_header("X-CyDaw", "native")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

with tempfile.TemporaryDirectory(prefix="cydaw-native-platform-") as folder:
    archive = pathlib.Path(folder) / "archive-é.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zip:
        zip.writestr("media/large.bin", bytes(i % 251 for i in range(1048576)))
        zip.writestr("é.xml", "<project/>")
        zip.writestr("../outside", "metadata")
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        result = subprocess.run([sys.argv[1], f"http://127.0.0.1:{server.server_port}", str(archive)], timeout=30)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    sys.exit(result.returncode)
