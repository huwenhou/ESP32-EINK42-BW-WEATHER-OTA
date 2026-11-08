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
<title>Wi-Fi · Robbie's Weather Board</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#F2F2F7;color:#000;font:17px -apple-system,BlinkMacSystemFont,"PingFang SC","Microsoft YaHei",sans-serif}
main{max-width:480px;margin:0 auto;padding:28px 16px 28px}
h1{margin:0 0 4px 4px;font-size:34px}.sub{margin:0 0 18px 4px;font-size:13px;color:#6C6C70}
.cap{font-size:13px;color:#6C6C70;margin:18px 0 6px 16px}.cap:first-of-type{margin-top:0}
.grp{background:#fff;border-radius:12px;overflow:hidden}
.row{display:flex;align-items:center;gap:10px;min-height:48px;padding:0 16px;border-bottom:.5px solid #E5E5EA;cursor:pointer}
.row:last-child{border-bottom:0}.row input{position:absolute;opacity:0;pointer-events:none}
.ck{width:18px;color:#0063D1;font-weight:600;visibility:hidden}input:checked+.ck{visibility:visible}
.n{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.oth{color:#0063D1;padding-left:44px}
.f{display:flex;align-items:center;min-height:48px;padding:0 16px;gap:12px;border-bottom:.5px solid #E5E5EA}.f:last-child{border-bottom:0}
.f label{width:64px;flex:none}.f input{flex:1;border:0;outline:0;font:inherit;height:44px;background:transparent;min-width:0}
button,.btn{display:block;width:100%;margin-top:22px;height:50px;border:0;border-radius:12px;background:#0063D1;color:#fff;font-weight:600;font-size:17px;font-family:inherit;text-align:center;line-height:50px;text-decoration:none}
.foot{margin-top:18px;font-size:12px;color:#6C6C70;text-align:center}.foot a{color:#0063D1;text-decoration:none}
.msg{text-align:center;padding-top:60px}.msg h1{margin:18px 0 8px}.msg p{color:#3C3C43;line-height:1.6;margin:0}
.spin{width:44px;height:44px;margin:0 auto;border:4px solid #D1D1D6;border-top-color:#0063D1;border-radius:50%;animation:s 1s linear infinite}@keyframes s{to{transform:rotate(1turn)}}
#manual{display:none}
</style></head><body><main>)HTML";

const char PAGE_FORM[] PROGMEM = R"HTML(
<h1>Wi-Fi</h1><p class="sub">为 Robbie's Weather Board 选择一个网络</p>
<form method="post" action="/join" autocomplete="off">
<div class="cap">网络</div><div class="grp">%LIST%
<label class="row oth"><input type="radio" name="s" value="" id="o">其他…</label></div>
<div class="grp" id="manual" style="margin-top:10px"><div class="f"><label for="m">名称</label><input id="m" name="m" placeholder="网络名称"></div></div>
<div class="cap" id="pc">密码</div>
<div class="grp"><div class="f"><label for="p">密码</label><input id="p" name="p" type="password" placeholder="开放网络可留空"></div></div>
<button type="submit">加入</button></form>
<p class="foot">天气看板 · 固件 v%VER% · <a href="/scan">重新扫描</a></p>
<script>
var f=document.forms[0],man=document.getElementById('manual'),pc=document.getElementById('pc');
f.addEventListener('change',function(e){if(e.target.name!='s')return;var v=e.target.value;
man.style.display=v?'none':'block';pc.textContent=v?'输入“'+v+'”的密码':'密码';if(!v)document.getElementById('m').focus();});
f.addEventListener('submit',function(e){var c=f.querySelector('input[name=s]:checked');if(!c||(!c.value&&!f.m.value)){e.preventDefault();alert('请先选择一个网络');}});
</script>)HTML";

const char PAGE_WAIT[] PROGMEM = R"HTML(
<div class="msg"><div class="spin" id="sp"></div><h1 id="t">正在连接…</h1><p id="d">%SSID%</p><a class="btn" id="b" href="/" style="display:none">返回</a></div>
<script>
function q(){fetch('/status').then(function(r){return r.text()}).then(function(s){
if(s=='ok'){sp.outerHTML='<svg width="72" height="72" viewBox="0 0 72 72"><circle cx="36" cy="36" r="34" fill="none" stroke="#34C759" stroke-width="3"/><path d="M22 37l9 9 19-20" fill="none" stroke="#34C759" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/></svg>';
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
    const char* c2 = r > -75 ? "#000" : "#C7C7CC";
    const char* c3 = r > -62 ? "#000" : "#C7C7CC";
    String s = esc(WiFi.SSID(i));
    listHtml += "<label class=\"row\"><input type=\"radio\" name=\"s\" value=\"" + s + "\"><span class=\"ck\">✓</span><span class=\"n\">" + s + "</span>";
    if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN)
      listHtml += "<svg width=\"13\" height=\"16\" viewBox=\"0 0 14 16\"><rect x=\"1\" y=\"7\" width=\"12\" height=\"8\" rx=\"2\"/><path d=\"M4 7V5a3 3 0 0 1 6 0v2\" fill=\"none\" stroke=\"#000\" stroke-width=\"1.8\"/></svg>";
    listHtml += String("<svg width=\"18\" height=\"14\" viewBox=\"0 0 24 18\"><path d=\"M2 6a15 15 0 0 1 20 0\" fill=\"none\" stroke=\"") + c3 +
                "\" stroke-width=\"2.4\" stroke-linecap=\"round\"/><path d=\"M5.5 9.8a10 10 0 0 1 13 0\" fill=\"none\" stroke=\"" + c2 +
                "\" stroke-width=\"2.4\" stroke-linecap=\"round\"/><circle cx=\"12\" cy=\"15\" r=\"2\"/></svg></label>";
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
  b.replace("%VER%", String(FW_VERSION));
  send(b);
}
void handleScan() { scan(); toRoot(); }
void handleJoin() {
  String s = server.arg("s");
  if (s.isEmpty()) s = server.arg("m");
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
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_NAME);
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
