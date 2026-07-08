"""
售货机手机端服务 v4 —— OneNET HTTP API
用法: python server.py --port 8080

使用 OneNET HTTP API 直连平台：
  - 数据查询: GET /thingmodel/query-device-property
  - 命令下发: POST /thingmodel/set-device-property (用物模型属性 DISPENSE_0 等)
不占用设备 MQTT 连接，不会踢设备下线。
"""
import http.server
import json
import ssl
import sys
import os
import time
import hmac
import base64
import hashlib
import urllib.parse
import urllib.request
import threading

PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
WEB_DIR = os.path.dirname(os.path.abspath(__file__))

# ==================== 安全打印 ====================
def _safe_print(msg):
    try:
        print(msg)
    except UnicodeEncodeError:
        print(msg.encode("ascii", errors="replace").decode("ascii"))

# ==================== 凭据加载 ====================
SECRET_FILE = os.path.join(WEB_DIR, "secret.js")
PRODUCT_ID = "gtx9E8EU09"
DEVICE_NAME = "dev1"
ACCESS_KEY = ""

try:
    with open(SECRET_FILE, "r", encoding="utf-8") as f:
        content = f.read()
    import re
    def _get(k):
        m = re.search(r"(?:'?" + k + r"'?)\s*:\s*'([^']+)'", content)
        return m.group(1) if m else None
    if _get("productId"):   PRODUCT_ID = _get("productId")
    if _get("deviceName"):  DEVICE_NAME = _get("deviceName")
    if _get("accessKey"):   ACCESS_KEY = _get("accessKey")
    _safe_print(f"[OK] Loaded secret.js: {PRODUCT_ID}/{DEVICE_NAME}")
except Exception as e:
    _safe_print(f"[WARN] Using built-in credentials ({e})")

API_BASE = "https://iot-api.heclouds.com"

# ==================== HTTP API Token ====================
_token = None
_token_time = 0

def make_token():
    if not ACCESS_KEY:
        raise RuntimeError("ACCESS_KEY is empty")
    et = str(int(time.time()) + 86400)
    method = "sha1"
    res = f"products/{PRODUCT_ID}"
    version = "2018-10-31"
    sign_str = f"{et}\n{method}\n{res}\n{version}"
    key_bytes = base64.b64decode(ACCESS_KEY)
    sig = hmac.new(key_bytes, sign_str.encode(), hashlib.sha1).digest()
    sig_b64 = base64.b64encode(sig).decode()
    sig_enc = urllib.parse.quote(sig_b64, safe='')
    token = (f"version={version}"
             f"&res={urllib.parse.quote(res, safe='')}"
             f"&et={et}"
             f"&method={method}"
             f"&sign={sig_enc}")
    return token

def get_token():
    global _token, _token_time
    now = time.time()
    if _token is None or (now - _token_time) > 3600:
        _token = make_token()
        _token_time = now
        _safe_print("[AUTH] Token refreshed")
    return _token

# ==================== HTTP API 封装 ====================
def _api_req(method, path, body=None):
    url = f"{API_BASE}{path}"
    data = json.dumps(body).encode() if body else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", get_token())
    if data:
        req.add_header("Content-Type", "application/json")

    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE

    resp = urllib.request.urlopen(req, timeout=10, context=ctx)
    return json.loads(resp.read().decode())

def api_query():
    """查询设备最新属性数据"""
    try:
        path = f"/thingmodel/query-device-property?product_id={PRODUCT_ID}&device_name={DEVICE_NAME}"
        data = _api_req("GET", path)

        if data.get("code") != 0:
            _safe_print(f"[WARN] Query failed: {data.get('msg', data)}")
            return None

        props = data.get("data", {})
        if isinstance(props, list):
            props = props
        elif isinstance(props, dict):
            props = props.get("properties", []) or props.get("list", []) or []
        else:
            props = []

        result = {}
        for p in props:
            if isinstance(p, dict):
                pid = p.get("id") or p.get("identifier") or p.get("name", "")
                pv = p.get("value")
                if pid and pv is not None:
                    try:
                        pv = float(pv)
                        if pv == int(pv):
                            pv = int(pv)
                    except (ValueError, TypeError):
                        pass
                    result[pid] = pv
        return result
    except Exception as e:
        _safe_print(f"[ERR] API query: {e}")
        return None

def api_dispense(idx):
    """
    调用设备服务 DISPENSE_0/2/4/6（服务调用）。
    API: POST /thingmodel/call-service
    idx: 0=Apple, 2=Banana, 4=Orange, 6=Mango
    """
    service_id = f"DISPENSE_{idx}"
    try:
        data = _api_req("POST", "/thingmodel/call-service", {
            "product_id": PRODUCT_ID,
            "device_name": DEVICE_NAME,
            "identifier": service_id,
            "params": {}
        })
        if data.get("code") == 0:
            _safe_print(f"[SEND] OK: service {service_id} invoked")
            return True, "ok"
        else:
            msg = data.get("msg", str(data))
            _safe_print(f"[SEND] FAIL: {service_id} -> {msg}")
            return False, msg
    except Exception as e:
        _safe_print(f"[ERR] Invoke {service_id}: {e}")
        return False, str(e)

# ==================== 后台轮询 ====================
latest_data = {
    "temp": None, "humi": None,
    "Apple": None, "Banana": None,
    "Orange": None, "Mango": None
}
api_connected = False

def poll_thread():
    global latest_data, api_connected
    time.sleep(2)
    while True:
        try:
            result = api_query()
            if result:
                api_connected = True
                for key in latest_data:
                    if key in result:
                        latest_data[key] = result[key]
            else:
                api_connected = False
        except Exception as e:
            api_connected = False
            _safe_print(f"[ERR] Poll: {e}")
        time.sleep(5)

# ==================== HTTP 服务 ====================
class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEB_DIR, **kwargs)

    def do_GET(self):
        if self.path == "/data":
            self._json_response({
                "connected": api_connected,
                "data": latest_data
            })
        else:
            super().do_GET()

    def do_POST(self):
        if self.path == "/dispense":
            length = int(self.headers.get("Content-Length", 0))
            body = json.loads(self.rfile.read(length)) if length > 0 else {}
            idx = body.get("idx", 0)

            _safe_print(f"[REQ] Dispense idx={idx}")
            ok, msg = api_dispense(idx)
            if ok:
                self._json_response({"ok": True, "msg": f"DISPENSE_{idx} sent"})
            else:
                self._json_response({"ok": False, "msg": msg}, 503)
        else:
            self.send_error(404)

    def _json_response(self, data, code=200):
        body = json.dumps(data, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        if "/data" not in str(args[0]):
            _safe_print(f"  {args[0]}")

# ==================== 启动 ====================
if __name__ == "__main__":
    t = threading.Thread(target=poll_thread, daemon=True)
    t.start()

    _safe_print("=" * 50)
    _safe_print("Vending Machine Server v4 (OneNET HTTP API)")
    _safe_print(f"  Platform: {API_BASE}")
    _safe_print(f"  Device:   {PRODUCT_ID}/{DEVICE_NAME}")
    _safe_print(f"  Port:     {PORT}")
    _safe_print(f"  Mobile:   http://<your-ip>:{PORT}/mobile.html")
    _safe_print("=" * 50)

    http.server.HTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
