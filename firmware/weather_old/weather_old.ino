// =====================================================================
//  4.2" 黑白墨水屏 · 天气看板 · 老款 v1.0（HINK-E042A03-A1 双 SSD1608）
//  · 数据：Open-Meteo（免费、无需密钥）
//  · 左：当前天气；右：未来 6 天
//  · 省电：每个整点醒来 → 联网取天气 → 刷屏 → 关 Wi-Fi → 深度睡眠到下个整点
//  · 只在每天 0 点那次醒来时检查 OTA 新固件
// =====================================================================
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <SPI.h>
#include <time.h>
#include <math.h>
#include <esp_partition.h>
#include <esp_sleep.h>
#include <ArduinoJson.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "hink042.h"             // 老款屏 HINK-E042A03-A1（双 SSD1608）驱动
#include "version.h"
#include "config.h"
#include "icons.h"
#include "Barlow_Bold33pt7b.h"   // CI 生成
#include "Barlow_Bold14pt7b.h"   // CI 生成（未来 6 天温度）

typedef void (*DrawFn)();

#define NUM_XL &Barlow_Bold33pt7b
#define NUM_M  &Barlow_Bold14pt7b
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

HINK042 epd(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
Adafruit_GFX* G = &epd;
U8G2_FOR_ADAFRUIT_GFX u8f;
const uint16_t BLACK = HINK042::BLACK, WHITE = HINK042::WHITE;


// ---------------------------------------------------------------------
//  天气数据
// ---------------------------------------------------------------------
struct Day { int y, m, d, code; float tmax, tmin; };
struct Wx {               // 放在 RTC 内存里，深度睡眠后保留上一次的数据
  bool ok;
  float temp, feels, windSpd, windDir;
  int hum, code;
  Day day[7];
  int nDays;
  char sunrise[6], sunset[6], updated[6];
  bool stale;
};
RTC_DATA_ATTR Wx wx;
RTC_DATA_ATTR uint32_t bootCount = 0;
char statusL1[48], statusL2[64];
bool viaOta = false;   // 当前固件是否经 OTA 写入（右上角显示“OTA更新”）

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
  snprintf(buf, sizeof(buf), wx.stale ? "离线 %s" : (viaOta ? "OTA更新 %s" : "更新 %s"), wx.updated);
  cnR(390, 20, buf, CN_M, WHITE);

  // --- 左：当前 ---
  const WCode& now = wcode(wx.code);
  drawWIcon(*G, now.icon, 38, 34, 84, BLACK, WHITE);

  snprintf(buf, sizeof(buf), "%d", r0(wx.temp));
  G->setFont(NUM_XL);
  G->setTextColor(BLACK);
  G->setTextWrap(false);
  // 数字 + 度数圈 整体在左栏（0~161）内水平居中
  int tw = numW(buf, NUM_XL) + 9 + 8;
  int tx = (161 - tw) / 2;
  G->setCursor(tx, 174);
  G->print(buf);
  int dx = tx + numW(buf, NUM_XL) + 9;     // 度数圈
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

  // --- 右：未来 6 天（温度用大号数字 "低~高°"，右对齐）---
  for (int i = 1; i < wx.nDays && i <= 6; i++) {
    const Day& d = wx.day[i];
    int top = 31 + (i - 1) * 44;
    cn(173, top + 24, WEEK[dayOfWeek(d.y, d.m, d.d)], CN_L, BLACK, true);
    snprintf(buf, sizeof(buf), "%d/%d", d.m, d.d);
    cn(173, top + 40, buf, CN_M);
    const WCode& w = wcode(d.code);
    drawWIcon(*G, w.icon, 212, top + 7, 34, BLACK, WHITE);
    cn(252, top + 28, w.text, CN_L);

    snprintf(buf, sizeof(buf), "%d~%d", r0(d.tmin), r0(d.tmax));
    const int XR = 392;                       // 度数圈右边缘
    int nx = XR - 7 - numW(buf, NUM_M);
    G->setFont(NUM_M);
    G->setTextColor(BLACK);
    G->setCursor(nx, top + 31);
    G->print(buf);
    G->fillCircle(XR - 3, top + 15, 3, BLACK);
    G->fillCircle(XR - 3, top + 15, 1, WHITE);

    if (i < 6) for (int x = 172; x < 394; x += 3) G->drawPixel(x, top + 44, BLACK);
  }
}

void drawStatus() {
  G->fillScreen(WHITE);
  G->fillRect(0, 0, 400, 27, BLACK);
  cn(10, 20, "天气看板", CN_L, WHITE, true);
  cnR(390, 20, FW_LABEL, CN_M, WHITE);
  cn(200 - cnW(statusL1, CN_L) / 2, 140, statusL1, CN_L, BLACK, true);
  cn(200 - cnW(statusL2, CN_M) / 2, 172, statusL2, CN_M);
  cn(10, 290, "屏幕 HINK-E042A03（双 SSD1608）", CN_M);
}

// 整屏缓冲：先画到内存，再一次性刷屏（约 6 秒），刷完屏幕自动深度睡眠
void render(DrawFn fn) {
  fn();
  epd.display();
}
void showStatus(const char* a, const char* b) {
  strlcpy(statusL1, a, sizeof(statusL1));
  strlcpy(statusL2, b, sizeof(statusL2));
  render(drawStatus);
}

// ---------------------------------------------------------------------
//  OTA
// ---------------------------------------------------------------------
// USB 烧录（arduino-cli / 网页刷机）会写入 boot_app0.bin：otadata 两个扇区的序号固定为 1 和 0；
// 每做一次 OTA，序号就会 +1 写进另一扇区（≥2）。据此判断当前固件来源，
// 不依赖旧固件配合，从旧版本 OTA 升上来也能识别；之后再 USB 烧录会自动恢复。
bool firmwareFromOta() {
  const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
  if (!p) return false;
  for (uint32_t off = 0; off < 2 * 0x1000 && off < p->size; off += 0x1000) {
    uint32_t seq = 0xFFFFFFFF;
    if (esp_partition_read(p, off, &seq, sizeof(seq)) == ESP_OK && seq != 0xFFFFFFFF && seq >= 2) return true;
  }
  return false;
}

// 依次尝试主地址（自定义域名）和备用地址（github.io），返回可用地址的版本号
const char* OTA_BASES[] = { FW_BASE_URL, FW_FALLBACK_URL };
long fetchRemoteVersion(const char* base) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  String url = String(base) + "/version.txt?t=" + String(millis());
  if (!http.begin(client, url)) return -1;
  long v = -1;
  int code = http.GET();
  if (code == 200) { String b = http.getString(); b.trim(); v = b.toInt(); }
  else Serial.printf("%s version.txt HTTP %d\n", base, code);
  http.end();
  return v;
}
void checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); return; }
  for (const char* base : OTA_BASES) {
    long remote = fetchRemoteVersion(base);
    Serial.printf("local v%d, remote v%ld (%s)\n", FW_VERSION, remote, base);
    if (remote < 0) continue;            // 这个地址连不上，换下一个
    if (remote <= FW_VERSION) return;    // 已是最新
    WiFiClientSecure client;
    client.setInsecure();
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    httpUpdate.rebootOnUpdate(true);
    if (httpUpdate.update(client, String(base) + "/firmware.bin") == HTTP_UPDATE_FAILED)
      Serial.printf("Update failed: %s\n", httpUpdate.getLastErrorString().c_str());
    else return;
  }
}

void refreshWeather() {
  if (fetchWeather()) render(drawWeather);
  else if (wx.ok) { wx.stale = true; render(drawWeather); }
  else showStatus("天气获取失败", "下个整点自动重试，请检查网络");
}

// 关 Wi-Fi，深度睡眠到下一个整点（多睡 20 秒，确保醒来时已过整点）
void sleepUntilNextHour(bool timeOk) {
  uint64_t sec = 3600;
  struct tm t;
  if (timeOk && getLocalTime(&t, 100)) sec = 3600 - (t.tm_min * 60 + t.tm_sec) + 20;
  if (sec < 120) sec += 3600;
  Serial.printf("sleep %llu s\n", sec);
  Serial.flush();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_timer_wakeup(sec * 1000000ULL);
  esp_deep_sleep_start();
}

#include "portal.h"

void setup() {
  Serial.begin(115200);
  bootCount++;
  bool timerWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
  viaOta = firmwareFromOta();
  Serial.printf("FW v%d, via %s, %s wake #%u\n", FW_VERSION, viaOta ? "OTA" : "USB",
                timerWake ? "timer" : "power/reset", bootCount);

  epd.begin(EPD_SCK, EPD_MOSI);
  u8f.begin(*G);

  bool connected;
  if (timerWake) {
    // 定时唤醒：只用已保存的 Wi-Fi，连不上就算了，不开热点（省电）
    connected = portal::connectSaved(20000);
  } else {
    // 上电 / 按 RESET：连不上就开热点配网（5 分钟无人操作则退出，下个整点再试）
    showStatus("正在连接 Wi-Fi", "首次使用请用手机连接热点配网");
    connected = portal::connectSaved(20000);
    if (!connected) {
      showStatus("请连接热点 " AP_NAME, "手机会自动弹出页面，选择 Wi-Fi 并输入密码");
      portal::run(300000);        // 配网成功会直接重启
    }
  }

  bool timeOk = false;
  if (connected) {
    configTzTime("CST-8", "ntp.aliyun.com", "ntp.tencent.com", "cn.pool.ntp.org");
    struct tm t;
    timeOk = getLocalTime(&t, 8000);
#if ENABLE_OTA
    // 每天只在 0 点这次唤醒时检查新固件
    if (timeOk && t.tm_hour == 0) checkForUpdate();
#endif
    refreshWeather();
  } else {
    if (wx.ok) { wx.stale = true; render(drawWeather); }
    else showStatus("Wi-Fi 连接失败", "下个整点自动重试；按 RESET 可重新配网");
  }
  sleepUntilNextHour(timeOk);
}

void loop() {}
