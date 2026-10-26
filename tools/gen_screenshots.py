#!/usr/bin/env python3
"""Regenerate the screenshots of the web page (assets/ui-*.png) for the README and the website.

The page (open-firenet/web/index.html) is served by a small local server that answers the API calls with the
fixtures of tools/screenshot_fixtures/ (a DOMO 2.29 in standby, anonymised), and is captured with headless Chromium.
Each shot is rendered in an iframe of the wanted size, which also gives real phone widths (a headless window cannot be
narrower than 500 px).

    python3 tools/gen_screenshots.py            # writes assets/ui-*.png
    python3 tools/gen_screenshots.py --out DIR  # writes somewhere else
    python3 tools/gen_screenshots.py --serve    # no screenshot: serves the page with the example data on port 8080

Needs: chromium (or chromium-browser / google-chrome) and Pillow.
"""
import argparse, http.server, json, pathlib, re, shutil, subprocess, sys, tempfile, threading
from urllib.parse import urlparse, parse_qs

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAGE = ROOT / "open-firenet" / "web" / "index.html"
FIX = ROOT / "tools" / "screenshot_fixtures"
# The page shows the firmware version beside its title: the shots carry the one of the source, not the fixture's.
VERSION = re.search(r'#define OPENFIRENET_VERSION "([^"]+)"', (ROOT / "open-firenet" / "open-firenet.ino").read_text()).group(1)


def fixture(name):
    body = (FIX / name).read_bytes()
    if name == "state.json":
        state = json.loads(body)
        for key in ("version", "app_version", "firmware_version"):
            state["device"][key] = VERSION
        body = json.dumps(state).encode()
    return body

# name, width, height, page (#hash), JavaScript run in the page once loaded
SHOTS = [
    ("ui-desktop-controls",    1280, 1260, "stove",       ""),
    ("ui-desktop-schedule",    1280, 1260, "stove",       "showCtrlTab('ctrl-sched')"),
    ("ui-desktop-bridge",      1280,  900, "bridge",      "showBridgeTab('bridge-net')"),
    ("ui-desktop-bridge-mqtt", 1280,  860, "bridge",      "showBridgeTab('bridge-mqtt')"),
    ("ui-desktop-bridge-system", 1280, 760, "bridge",     "showBridgeTab('bridge-system')"),
    ("ui-desktop-bridge-screen", 1280, 960, "bridge",     "showBridgeTab('bridge-screen')"),
    ("ui-desktop-diagnostics", 1280, 1260, "diagnostics", ""),
    ("ui-mobile",               430, 2250, "stove",       ""),
    ("ui-mobile-bridge",        430, 1100, "bridge",      "showBridgeTab('bridge-net')"),
]
API = {"/api/state": "state.json", "/api/controls": "controls.json", "/api/control": "controls.json",
       "/api/schedule": "schedule.json", "/api/txgap": "txgap.json", "/api/mqtt": "mqtt.json",
       "/api/display": "display.json"}
SCAN = [{"ssid": "MyHomeWiFi", "rssi": -51, "secure": True}, {"ssid": "Neighbour", "rssi": -78, "secure": True}]


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, body, ctype="application/json"):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/shot":   # wrapper: sets the language, then shows the page in an iframe of the wanted size
            q = {k: v[0] for k, v in parse_qs(url.query).items()}
            js = json.dumps(q.get("js", ""))
            self.reply((
                "<body style='margin:0;background:#0b0e14;overflow:hidden'><script>"
                "localStorage.setItem('lang','en');"
                "const f=document.createElement('iframe');"
                f"f.width={int(q['w'])};f.height={int(q['h'])};f.scrolling='no';f.style='border:0;display:block';"
                f"f.src='/#{q['page']}';"
                f"f.onload=()=>setTimeout(()=>{{const js={js};if(js)f.contentWindow.eval(js)}},1500);"
                "document.body.appendChild(f)</script></body>").encode(), "text/html")
        elif url.path == "/":
            self.reply(PAGE.read_bytes(), "text/html; charset=utf-8")
        elif url.path in API:
            self.reply(fixture(API[url.path]))
        elif url.path == "/log":
            self.reply((FIX / "log.txt").read_bytes(), "text/plain")
        elif url.path == "/api/scan":
            self.reply(json.dumps(SCAN).encode())
        elif url.path == "/api/access":
            self.reply(json.dumps({"ok": True, "extra_hosts": [], "max_extra_hosts": 8, "host": "open-firenet.local"}).encode())
        elif url.path == "/api/wifi_power_saving":
            self.reply(json.dumps({"ok": True, "enabled": True}).encode())
        else:
            self.reply(b"{}")

    def do_POST(self):
        self.do_GET()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "assets"))
    ap.add_argument("--serve", action="store_true",
                    help="only serve the page with the example data, to look at it in a browser (translations, layout)")
    args = ap.parse_args()
    if args.serve:
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 8080), Handler)
        print("The web page, with example data (nothing is sent to a stove): http://127.0.0.1:8080  -  Ctrl+C to stop")
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            return
    chromium = next((c for c in ("chromium", "chromium-browser", "google-chrome") if shutil.which(c)), None)
    if not chromium:
        sys.exit("chromium not found")
    from PIL import Image
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for name, w, h, page, js in SHOTS:
            raw = pathlib.Path(tmp) / f"{name}.png"
            url = f"http://127.0.0.1:{port}/shot?w={w}&h={h}&page={page}&js={js}"
            subprocess.run([chromium, "--headless", "--no-sandbox", "--disable-gpu", "--hide-scrollbars",
                            f"--window-size={max(w, 500)},{h}", "--virtual-time-budget=8000",
                            f"--screenshot={raw}", url], check=True, capture_output=True)
            Image.open(raw).crop((0, 0, w, h)).save(out / f"{name}.png", optimize=True)
            print(f"wrote {out / (name + '.png')} ({w}x{h})")
    server.shutdown()


if __name__ == "__main__":
    main()
