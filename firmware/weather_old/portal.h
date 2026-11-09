// =====================================================================
//  Wi-Fi 配网（自写，替代 WiFiManager）
//  · 页面风格：系统设置（E 款），只用手机自带字体，不依赖外网
//  · 强制门户：DNS 全部指向本机 + 应答 iOS / Android / Windows 的联网检测地址，
//    手机连上热点后自动弹出页面
//  · 账号存在 Preferences("wifi")；兼容 WiFiManager 时代存在系统里的旧账号
//  依赖：在 weather.ino 中 showStatus()、FW_VERSION、AP_NAME 之后 #include
// =====================================================================
#pragma once
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <vector>
#include <algorithm>

namespace portal {

const IPAddress AP_IP(192, 168, 4, 1);
WebServer server(80);
DNSServer dns;
String listHtml;
String tryingSsid, tryingPass;
enum JoinState { IDLE, TRYING, OK, FAIL } joinState = IDLE;
unsigned long joinStart = 0, lastAct = 0;   // lastAct：最近一次有人访问页面

String esc(const String& s) {
  String o; o.reserve(s.length() + 8);
  for (char c : s) {
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;"; else if (c == '\'') o += "&#39;"; else o += c;
  }
  return o;
}

const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>连接 Wi-Fi · Robbie's Weather Board</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#F3F1EA;color:#121212;font:16px/1.5 -apple-system,BlinkMacSystemFont,"PingFang SC","Microsoft YaHei",sans-serif}
main{max-width:480px;margin:0 auto;padding:40px 20px 28px;min-height:100vh;display:flex;flex-direction:column;gap:24px}
.k{font-size:13px;font-weight:600;color:#C4161C;letter-spacing:.04em}h1{margin:4px 0 6px;font-size:30px;line-height:1.25}.lead{margin:0;color:#4A4A47;font-size:15px}
.card{background:#fff;border-radius:14px;overflow:hidden}
details{border-bottom:1px solid #ECE9E0}details:last-child{border-bottom:0}
summary{list-style:none;display:flex;align-items:center;gap:12px;min-height:56px;padding:0 16px;cursor:pointer}summary::-webkit-details-marker{display:none}
.n{flex:1;font-weight:500;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.oth summary{color:#4A4A47;font-size:15px}
form{display:flex;flex-direction:column;gap:12px;padding:4px 16px 16px}
label{display:flex;flex-direction:column;gap:6px;font-size:13px;color:#4A4A47}
input{height:48px;border:1.5px solid #121212;border-radius:10px;padding:0 14px;font:inherit;font-size:17px;background:#fff;color:#121212;width:100%}
button,.btn{height:50px;border:0;border-radius:10px;background:#C4161C;color:#fff;font:inherit;font-size:17px;font-weight:600;cursor:pointer;display:block;text-align:center;line-height:50px;text-decoration:none}
.foot{margin-top:auto;display:flex;justify-content:space-between;font-size:12px;color:#6E6D68}.foot a{color:#C4161C;text-decoration:none}
.msg{text-align:center;margin-top:40px}.msg h1{margin:18px 0 8px}.msg p{color:#4A4A47;margin:0 0 24px}
.spin{width:44px;height:44px;margin:0 auto;border:4px solid #DEDBD1;border-top-color:#C4161C;border-radius:50%;animation:s 1s linear infinite}@keyframes s{to{transform:rotate(1turn)}}
</style></head><body><main>)HTML";

const char PAGE_FORM[] PROGMEM = R"HTML(
<div><div class="k">ROBBIE'S WEATHER BOARD</div><h1>连接 Wi-Fi</h1><p class="lead">选择家里的网络，天气看板会每小时自动更新。</p></div>
<div class="card">%LIST%
<details class="oth"><summary>其他网络（手动输入名称）…</summary><form method="post" action="/join" autocomplete="off">
<label>网络名称<input name="s" required></label><label>密码<input name="p" type="password" placeholder="开放网络可留空"></label><button>连接</button></form></details>
</div>
<div class="foot"><span>固件 v%VER%</span><a href="/scan">重新扫描</a></div>
<script>
var ds=document.querySelectorAll('details');
ds.forEach(function(d){d.addEventListener('toggle',function(){if(!d.open)return;
ds.forEach(function(o){if(o!==d)o.open=false});var i=d.querySelector('input:not([type=hidden])');if(i)setTimeout(function(){i.focus()},50);});});
</script>)HTML";

const char PAGE_WAIT[] PROGMEM = R"HTML(
<div class="msg"><div class="spin" id="sp"></div><h1 id="t">正在连接…</h1><p id="d">%SSID%</p><a class="btn" id="b" href="/" style="display:none">返回</a></div>
<script>
function q(){fetch('/status').then(function(r){return r.text()}).then(function(s){
if(s=='ok'){sp.outerHTML='<svg width="72" height="72" viewBox="0 0 72 72"><circle cx="36" cy="36" r="34" fill="none" stroke="#1F6F43" stroke-width="3"/><path d="M22 37l9 9 19-20" fill="none" stroke="#1F6F43" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/></svg>';
t.textContent='已连接';d.innerHTML='约 30 秒后屏幕显示天气。<br>此热点即将关闭，可以关掉这个页面了。';}
else if(s=='fail'){sp.style.display='none';t.textContent='连接失败';d.textContent='密码不对，或者信号太弱。请返回重试。';b.style.display='block';}
else setTimeout(q,1500);}).catch(function(){setTimeout(q,2000)});}
var sp=document.getElementById('sp'),t=document.getElementById('t'),d=document.getElementById('d'),b=document.getElementById('b');setTimeout(q,1500);
</script>)HTML";

const char PAGE_TAIL[] PROGMEM = "</main></body></html>";

void scan() {
  int n = WiFi.scanNetworks(false, false);
  // 去重（同名多个热点取最强），按信号排序
  std::vector<int> idx;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.isEmpty()) continue;
    bool dup = false;
    for (size_t k = 0; k < idx.size(); k++)
      if (WiFi.SSID(idx[k]) == s) { dup = true; if (WiFi.RSSI(i) > WiFi.RSSI(idx[k])) idx[k] = i; break; }
    if (!dup) idx.push_back(i);
  }
  std::sort(idx.begin(), idx.end(), [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });
  listHtml = "";
  int shown = 0;
  for (int i : idx) {
    if (shown++ >= 15) break;
    int r = WiFi.RSSI(i);
    const char* on = "#121212"; const char* off = "#D6D3C9";
    const char* b2 = r > -80 ? on : off; const char* b3 = r > -70 ? on : off; const char* b4 = r > -60 ? on : off;
    bool locked = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    String s = esc(WiFi.SSID(i));
    listHtml += String("<details><summary><svg width=\"20\" height=\"16\" viewBox=\"0 0 20 16\"><rect y=\"11\" width=\"3.5\" height=\"5\" rx=\"1\" fill=\"#121212\"/><rect x=\"5.5\" y=\"7.5\" width=\"3.5\" height=\"8.5\" rx=\"1\" fill=\"") + b2 +
      "\"/><rect x=\"11\" y=\"4\" width=\"3.5\" height=\"12\" rx=\"1\" fill=\"" + b3 + "\"/><rect x=\"16.5\" width=\"3.5\" height=\"16\" rx=\"1\" fill=\"" + b4 +
      "\"/></svg><span class=\"n\">" + s + "</span>";
    if (locked) listHtml += "<svg width=\"14\" height=\"16\" viewBox=\"0 0 14 16\"><rect x=\"1\" y=\"7\" width=\"12\" height=\"8\" rx=\"2\" fill=\"none\" stroke=\"#6E6D68\" stroke-width=\"1.6\"/><path d=\"M4 7V5a3 3 0 0 1 6 0v2\" fill=\"none\" stroke=\"#6E6D68\" stroke-width=\"1.6\"/></svg>";
    listHtml += "</summary><form method=\"post\" action=\"/join\" autocomplete=\"off\"><input type=\"hidden\" name=\"s\" value=\"" + s + "\">";
    if (locked) listHtml += "<label>密码<input name=\"p\" type=\"password\" required></label>";
    listHtml += "<button>连接</button></form></details>";
  }
  WiFi.scanDelete();
}

void send(const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", String(FPSTR(PAGE_HEAD)) + body + FPSTR(PAGE_TAIL));
}
void toRoot() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}
// 访问的不是本机地址（captive.apple.com 等）一律跳到首页
bool captive() {
  if (server.hostHeader() != AP_IP.toString()) { toRoot(); return true; }
  return false;
}

void handleRoot() {
  if (captive()) return;
  lastAct = millis();
  String b = FPSTR(PAGE_FORM);
  b.replace("%LIST%", listHtml);
  b.replace("%VER%", String(FW_LABEL));
  send(b);
}
void handleScan() { scan(); toRoot(); }
void handleJoin() {
  String s = server.arg("s");
  s.trim();
  if (s.isEmpty()) { toRoot(); return; }
  lastAct = millis();
  tryingSsid = s;
  tryingPass = server.arg("p");
  String b = FPSTR(PAGE_WAIT);
  b.replace("%SSID%", esc(s));
  send(b);
  delay(50);
  WiFi.disconnect(false, false);
  WiFi.begin(tryingSsid.c_str(), tryingPass.c_str());
  joinState = TRYING;
  joinStart = millis();
}
void handleStatus() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain", joinState == OK ? "ok" : joinState == FAIL ? "fail" : "ing");
}

void saveCreds(const String& s, const String& p) {
  Preferences pr; pr.begin("wifi", false);
  pr.putString("ssid", s); pr.putString("pass", p);
  pr.end();
}

// 连接已保存的 Wi-Fi；先用自己保存的，没有再试 WiFiManager 留在系统里的旧账号
bool connectSaved(uint32_t timeoutMs) {
  Preferences pr; pr.begin("wifi", true);
  String s = pr.getString("ssid", ""), p = pr.getString("pass", "");
  pr.end();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  if (s.length()) WiFi.begin(s.c_str(), p.c_str());
  else WiFi.begin();                       // 旧版 WiFiManager 存的账号
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(200);
  if (WiFi.status() != WL_CONNECTED) return false;
  if (s.isEmpty()) saveCreds(WiFi.SSID(), WiFi.psk());   // 迁移旧账号
  return true;
}

// 开热点配网；成功后重启，超时返回 false
bool run(uint32_t timeoutMs) {
  WiFi.persistent(false);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_AP_STA);
  scan();
  // 第 5 个参数是 DHCP 下发给手机的 DNS：不填会下发 0.0.0.0，手机就查不到任何域名、不会弹页面
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0), IPAddress(192, 168, 4, 2), AP_IP);
  WiFi.softAP(AP_NAME);
  WiFi.AP.enableDhcpCaptivePortal();   // DHCP 选项 114：iOS 14+ / Android 11+ 直接据此弹出页面
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", AP_IP);

  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/join", HTTP_POST, handleJoin);
  server.on("/status", handleStatus);
  // 各系统的联网检测地址：不返回它们期待的内容，系统就会弹出登录页
  const char* probes[] = { "/hotspot-detect.html", "/library/test/success.html", "/generate_204", "/gen_204",
                           "/connecttest.txt", "/ncsi.txt", "/redirect", "/canonical.html", "/success.txt", "/fwlink" };
  for (const char* u : probes) server.on(u, toRoot);
  server.onNotFound(toRoot);
  server.begin();
  Serial.println("portal started");

  lastAct = millis();
  while (millis() - lastAct < timeoutMs || joinState == TRYING) {   // 5 分钟没人操作才退出
    dns.processNextRequest();
    server.handleClient();
    if (joinState == TRYING) {
      wl_status_t st = WiFi.status();
      if (st == WL_CONNECTED) {
        joinState = OK;
        saveCreds(tryingSsid, tryingPass);
        Serial.printf("joined %s\n", tryingSsid.c_str());
        showStatus("Wi-Fi 已连接", tryingSsid.c_str());
        unsigned long t1 = millis();
        while (millis() - t1 < 6000) { dns.processNextRequest(); server.handleClient(); delay(5); }  // 让页面拿到“已连接”
        ESP.restart();
      } else if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL || millis() - joinStart > 20000) {
        joinState = FAIL;
        WiFi.disconnect(false, false);
      }
    }
    delay(5);
  }
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  return false;
}

}  // namespace portal
