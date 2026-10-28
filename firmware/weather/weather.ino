// =====================================================================
//  4.2" 黑白墨水屏 · 天气看板
//  · 数据：Open-Meteo（免费、无需密钥），每 30 分钟更新
//  · 左：当前天气；右：未来 6 天
//  · 每 10 分钟检查 GitHub Pages 上的新固件，自动 OTA
// =====================================================================
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <SPI.h>
#include <time.h>
#include <math.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "version.h"
#include "config.h"
#include "icons.h"
#include "Barlow_Bold33pt7b.h"   // CI 生成

typedef void (*DrawFn)();

#define NUM_XL &Barlow_Bold33pt7b
#define CN_L u8g2_font_wqy16_t_gb2312
#define CN_M u8g2_font_wqy14_t_gb2312

// ---------- Waveshare ESP32 驱动板引脚 ----------
#define EPD_BUSY 25
#define EPD_RST  26
#define EPD_DC   27
#define EPD_CS   15
#define EPD_SCK  13
#define EPD_MOSI 14
#define BOOT_BTN 0

GxEPD2_BW<GxEPD2_420, GxEPD2_420::HEIGHT>
  dispUC(GxEPD2_420(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT>
  dispSSD(GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
enum { PANEL_UC = 1, PANEL_SSD = 2 };
int panel = PANEL_UC;
Adafruit_GFX* G = nullptr;
U8G2_FOR_ADAFRUIT_GFX u8f;
const uint16_t BLACK = GxEPD_BLACK, WHITE = GxEPD_WHITE;

const unsigned long OTA_INTERVAL_MS = 10UL * 60UL * 1000UL;
const unsigned long WX_INTERVAL_MS  = (unsigned long)WEATHER_MINUTES * 60UL * 1000UL;

// ---------------------------------------------------------------------
//  天气数据
// ---------------------------------------------------------------------
struct Day { int y, m, d, code; float tmax, tmin; };
struct Wx {
  bool ok = false;
  float temp, feels, windSpd, windDir;
  int hum, code;
  Day day[7];
  int nDays;
  char sunrise[6], sunset[6], updated[6];
  bool stale;
} wx;
char statusL1[48], statusL2[64];
unsigned long lastWx = 0, lastOta = 0;

int dayOfWeek(int y, int m, int d) {          // 0=周日
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
const char* WEEK[7] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

const char* windDirName(float deg) {
  static const char* n[8] = {"北风", "东北风", "东风", "东南风", "南风", "西南风", "西风", "西北风"};
  return n[((int)roundf(deg / 45.0f)) % 8];
}
int beaufort(float kmh) {
  const float lim[] = {1, 6, 12, 20, 29, 39, 50, 62, 75, 89, 103, 118};
  for (int i = 0; i < 12; i++) if (kmh < lim[i]) return i;
  return 12;
}

bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); return false; }
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + CITY_LAT +
    "&longitude=" + CITY_LON +
    "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"
    "&timezone=Asia%2FShanghai&forecast_days=7";
  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(20000);
  if (!http.begin(cli, url)) return false;
  int code = http.GET();
  if (code != 200) { Serial.printf("weather HTTP %d\n", code); http.end(); return false; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) { Serial.printf("weather JSON %s\n", err.c_str()); return false; }

  JsonObject cur = doc["current"];
  JsonObject dy = doc["daily"];
  if (cur.isNull() || dy.isNull()) return false;
  wx.temp = cur["temperature_2m"] | 0.0f;
  wx.feels = cur["apparent_temperature"] | 0.0f;
  wx.hum = cur["relative_humidity_2m"] | 0;
  wx.code = cur["weather_code"] | 3;
  wx.windSpd = cur["wind_speed_10m"] | 0.0f;
  wx.windDir = cur["wind_direction_10m"] | 0.0f;

  JsonArray dates = dy["time"];
  wx.nDays = min((int)dates.size(), 7);
  for (int i = 0; i < wx.nDays; i++) {
    const char* s = dates[i] | "2026-01-01";
    sscanf(s, "%d-%d-%d", &wx.day[i].y, &wx.day[i].m, &wx.day[i].d);
    wx.day[i].code = dy["weather_code"][i] | 3;
    wx.day[i].tmax = dy["temperature_2m_max"][i] | 0.0f;
    wx.day[i].tmin = dy["temperature_2m_min"][i] | 0.0f;
  }
  const char* sr = dy["sunrise"][0] | "T--:--";
  const char* ss = dy["sunset"][0] | "T--:--";
  const char* p;
  strlcpy(wx.sunrise, (p = strchr(sr, 'T')) ? p + 1 : "--:--", sizeof(wx.sunrise));
  strlcpy(wx.sunset, (p = strchr(ss, 'T')) ? p + 1 : "--:--", sizeof(wx.sunset));

  struct tm t;
  if (getLocalTime(&t, 2000)) snprintf(wx.updated, sizeof(wx.updated), "%02d:%02d", t.tm_hour, t.tm_min);
  else strlcpy(wx.updated, "--:--", sizeof(wx.updated));
  wx.ok = wx.nDays > 0;
  wx.stale = false;
  return wx.ok;
}

// ---------------------------------------------------------------------
//  绘图工具
// ---------------------------------------------------------------------
void cn(int x, int y, const char* s, const uint8_t* f, uint16_t c = BLACK, bool bold = false) {
  u8f.setFont(f);
  u8f.setFontMode(1);            // setFont 会把背景重置为不透明，必须每次设回透明
  u8f.setForegroundColor(c);
  for (int dx = 0; dx <= (bold ? 1 : 0); dx++) {
    u8f.setCursor(x + dx, y);
    u8f.print(s);
  }
}
int cnW(const char* s, const uint8_t* f) { u8f.setFont(f); return u8f.getUTF8Width(s); }
void cnR(int xr, int y, const char* s, const uint8_t* f, uint16_t c = BLACK, bool bold = false) {
  cn(xr - cnW(s, f) - (bold ? 1 : 0), y, s, f, c, bold);
}
int numW(const char* s, const GFXfont* f) {
  int w = 0;
  for (; *s; s++) {
    uint8_t ch = *s;
    if (ch >= f->first && ch <= f->last) w += f->glyph[ch - f->first].xAdvance;
  }
  return w;
}
int r0(float v) { return (int)lroundf(v); }

// ---------------------------------------------------------------------
//  整屏布局（参照设计图）
// ---------------------------------------------------------------------
void drawWeather() {
  G->fillScreen(WHITE);
  char buf[64];

  // --- 顶栏 ---
  G->fillRect(0, 0, 400, 27, BLACK);
  const Day& d0 = wx.day[0];
  snprintf(buf, sizeof(buf), "%s  %d月%d日 %s", CITY_NAME, d0.m, d0.d, WEEK[dayOfWeek(d0.y, d0.m, d0.d)]);
  cn(10, 20, buf, CN_L, WHITE, true);
  snprintf(buf, sizeof(buf), wx.stale ? "离线 %s" : "更新 %s", wx.updated);
  cnR(390, 20, buf, CN_M, WHITE);

  // --- 左：当前 ---
  const WCode& now = wcode(wx.code);
  drawWIcon(*G, now.icon, 38, 34, 84, BLACK, WHITE);

  snprintf(buf, sizeof(buf), "%d", r0(wx.temp));
  G->setFont(NUM_XL);
  G->setTextColor(BLACK);
  G->setTextWrap(false);
  G->setCursor(14, 174);
  G->print(buf);
  int dx = 14 + numW(buf, NUM_XL) + 9;     // 度数圈
  G->fillCircle(dx, 136, 8, BLACK);
  G->fillCircle(dx, 136, 4, WHITE);

  cn(15, 221, now.text, CN_L, BLACK, true);
  snprintf(buf, sizeof(buf), "%d~%d°", r0(d0.tmin), r0(d0.tmax));
  cn(15 + cnW(now.text, CN_L) + 10, 221, buf, CN_L, BLACK, true);

  snprintf(buf, sizeof(buf), "体感 %d°  湿度 %d%%", r0(wx.feels), wx.hum);
  cn(15, 251, buf, CN_M);
  snprintf(buf, sizeof(buf), "%s %d级", windDirName(wx.windDir), beaufort(wx.windSpd));
  cn(15, 271, buf, CN_M);
  snprintf(buf, sizeof(buf), "日出 %s  日落 %s", wx.sunrise, wx.sunset);
  cn(15, 291, buf, CN_M);

  G->fillRect(161, 33, 2, 260, BLACK);

  // --- 右：未来 6 天 ---
  float wkMin = 1e9, wkMax = -1e9;
  for (int i = 1; i < wx.nDays; i++) { wkMin = fminf(wkMin, wx.day[i].tmin); wkMax = fmaxf(wkMax, wx.day[i].tmax); }
  if (wkMax - wkMin < 1) wkMax = wkMin + 1;
  const int BX0 = 326, BX1 = 358;
  for (int i = 1; i < wx.nDays && i <= 6; i++) {
    const Day& d = wx.day[i];
    int top = 31 + (i - 1) * 44;
    cn(173, top + 24, WEEK[dayOfWeek(d.y, d.m, d.d)], CN_L, BLACK, true);
    snprintf(buf, sizeof(buf), "%d/%d", d.m, d.d);
    cn(173, top + 40, buf, CN_M);
    const WCode& w = wcode(d.code);
    drawWIcon(*G, w.icon, 212, top + 7, 34, BLACK, WHITE);
    cn(252, top + 28, w.text, CN_L);

    snprintf(buf, sizeof(buf), "%d°", r0(d.tmin));
    cnR(BX0 - 3, top + 28, buf, CN_M);
    int by = top + 22;
    G->drawFastHLine(BX0, by, BX1 - BX0, BLACK);
    int a = BX0 + (int)((d.tmin - wkMin) / (wkMax - wkMin) * (BX1 - BX0));
    int b = BX0 + (int)((d.tmax - wkMin) / (wkMax - wkMin) * (BX1 - BX0));
    if (b - a < 5) { a = min(a, BX1 - 5); b = a + 5; }
    G->fillRoundRect(a, by - 3, b - a, 7, 3, BLACK);
    snprintf(buf, sizeof(buf), "%d°", r0(d.tmax));
    cn(BX1 + 4, top + 28, buf, CN_M);

    if (i < 6) for (int x = 172; x < 394; x += 3) G->drawPixel(x, top + 44, BLACK);
  }
}

void drawStatus() {
  G->fillScreen(WHITE);
  G->fillRect(0, 0, 400, 27, BLACK);
  cn(10, 20, "天气看板", CN_L, WHITE, true);
  char v[16]; snprintf(v, sizeof(v), "v%d", FW_VERSION);
  cnR(390, 20, v, CN_M, WHITE);
  cn(200 - cnW(statusL1, CN_L) / 2, 140, statusL1, CN_L, BLACK, true);
  cn(200 - cnW(statusL2, CN_M) / 2, 172, statusL2, CN_M);
  cn(10, 290, panel == PANEL_SSD ? "屏幕驱动 SSD1683" : "屏幕驱动 UC8176", CN_M);
}

template <class D> void renderOn(D& d, DrawFn fn) {
  d.setRotation(0);
  d.setFullWindow();
  d.firstPage();
  do { fn(); } while (d.nextPage());
  d.hibernate();
}
void render(DrawFn fn) {
  if (panel == PANEL_SSD) renderOn(dispSSD, fn);
  else renderOn(dispUC, fn);
}
void showStatus(const char* a, const char* b) {
  strlcpy(statusL1, a, sizeof(statusL1));
  strlcpy(statusL2, b, sizeof(statusL2));
  render(drawStatus);
}

// 复位后读 BUSY 空闲电平：UC8176 空闲为高，SSD 系列空闲为低
int detectPanel() {
  if (FORCE_PANEL) return FORCE_PANEL;
  pinMode(EPD_BUSY, INPUT);
  pinMode(EPD_RST, OUTPUT);
  digitalWrite(EPD_RST, HIGH); delay(20);
  digitalWrite(EPD_RST, LOW);  delay(20);
  digitalWrite(EPD_RST, HIGH); delay(400);
  int hi = 0;
  for (int i = 0; i < 10; i++) { hi += digitalRead(EPD_BUSY); delay(5); }
  return hi >= 5 ? PANEL_UC : PANEL_SSD;
}

// ---------------------------------------------------------------------
//  OTA
// ---------------------------------------------------------------------
long fetchRemoteVersion() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // 换自定义域名后旧地址会 301 跳转
  http.setTimeout(15000);
  String url = String(FW_BASE_URL) + "/version.txt?t=" + String(millis());
  if (!http.begin(client, url)) return -1;
  long v = -1;
  if (http.GET() == 200) { String b = http.getString(); b.trim(); v = b.toInt(); }
  http.end();
  return v;
}
void checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); return; }
  long remote = fetchRemoteVersion();
  Serial.printf("local v%d, remote v%ld\n", FW_VERSION, remote);
  if (remote <= FW_VERSION) return;
  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(true);
  if (httpUpdate.update(client, String(FW_BASE_URL) + "/firmware.bin") == HTTP_UPDATE_FAILED)
    Serial.printf("Update failed: %s\n", httpUpdate.getLastErrorString().c_str());
}

void checkResetButton() {
  static unsigned long pressedAt = 0;
  if (digitalRead(BOOT_BTN) == LOW) {
    if (pressedAt == 0) pressedAt = millis();
    if (millis() - pressedAt >= 5000) {
      WiFiManager wm;
      wm.resetSettings();
      showStatus("Wi-Fi 已清除", "正在重启...");
      delay(1000);
      ESP.restart();
    }
  } else {
    pressedAt = 0;
  }
}

void refreshWeather() {
  if (fetchWeather()) render(drawWeather);
  else if (wx.ok) { wx.stale = true; render(drawWeather); }
  else showStatus("天气获取失败", "稍后自动重试，请检查网络");
}

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BTN, INPUT_PULLUP);
  panel = detectPanel();
  SPI.end();
  SPI.begin(EPD_SCK, 12, EPD_MOSI, EPD_CS);
  if (panel == PANEL_SSD) { dispSSD.init(115200, true, 2, false); G = &dispSSD; }
  else                    { dispUC.init(115200, true, 2, false);  G = &dispUC; }
  u8f.begin(*G);

  showStatus("正在连接 Wi-Fi", "首次使用请用手机连接热点配网");
  WiFiManager wm;
  wm.setAPCallback([](WiFiManager*) {
    showStatus("请连接热点 " AP_NAME, "在弹出的页面里选择 Wi-Fi 并输入密码");
  });
  wm.setConfigPortalTimeout(300);
  if (!wm.autoConnect(AP_NAME)) {
    showStatus("Wi-Fi 连接失败", "正在重启...");
    delay(3000);
    ESP.restart();
  }
  configTzTime("CST-8", "ntp.aliyun.com", "ntp.tencent.com", "cn.pool.ntp.org");
  struct tm t; getLocalTime(&t, 8000);

  checkForUpdate();
  lastOta = millis();
  refreshWeather();
  lastWx = millis();
}

void loop() {
  checkResetButton();
  unsigned long interval = wx.ok ? WX_INTERVAL_MS : 60UL * 1000UL;
  if (millis() - lastWx >= interval) {
    lastWx = millis();
    refreshWeather();
  }
  if (millis() - lastOta >= OTA_INTERVAL_MS) {
    lastOta = millis();
    checkForUpdate();
  }
  delay(100);
}
