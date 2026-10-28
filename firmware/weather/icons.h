// =====================================================================
//  天气图标：线条风格，按宽度 w 等比缩放（大图标 84，小图标 32）
//  坐标单位 u = w/100，云占 100×64，降水在下方 64~92
// =====================================================================
#pragma once
#include <Adafruit_GFX.h>

enum WIcon { WI_SUN, WI_PARTLY, WI_CLOUD, WI_OVERCAST, WI_FOG, WI_DRIZZLE,
             WI_RAIN, WI_SHOWER, WI_SNOW, WI_SLEET, WI_THUNDER };

struct WCode { int code; const char* text; WIcon icon; };
// WMO 天气代码 → 中文（最多 3 个字，右侧列表放得下）
static const WCode WCODES[] = {
  {0, "晴", WI_SUN}, {1, "少云", WI_PARTLY}, {2, "多云", WI_PARTLY}, {3, "阴", WI_OVERCAST},
  {45, "雾", WI_FOG}, {48, "冻雾", WI_FOG},
  {51, "毛毛雨", WI_DRIZZLE}, {53, "毛毛雨", WI_DRIZZLE}, {55, "毛毛雨", WI_DRIZZLE},
  {56, "冻雨", WI_SLEET}, {57, "冻雨", WI_SLEET},
  {61, "小雨", WI_RAIN}, {63, "中雨", WI_RAIN}, {65, "大雨", WI_RAIN},
  {66, "冻雨", WI_SLEET}, {67, "冻雨", WI_SLEET},
  {71, "小雪", WI_SNOW}, {73, "中雪", WI_SNOW}, {75, "大雪", WI_SNOW}, {77, "米雪", WI_SNOW},
  {80, "阵雨", WI_SHOWER}, {81, "阵雨", WI_SHOWER}, {82, "强阵雨", WI_SHOWER},
  {85, "阵雪", WI_SNOW}, {86, "阵雪", WI_SNOW},
  {95, "雷阵雨", WI_THUNDER}, {96, "雷阵雨", WI_THUNDER}, {99, "雷阵雨", WI_THUNDER},
};
inline const WCode& wcode(int c) {
  for (auto& w : WCODES) if (w.code == c) return w;
  return WCODES[3];
}

// 实心云：b 为向内收缩量（画白色内层形成描边）
inline void cloudFill(Adafruit_GFX& g, int x, int y, float u, int b, uint16_t c) {
  auto X = [&](float v) { return x + (int)(v * u); };
  auto Y = [&](float v) { return y + (int)(v * u); };
  int r1 = (int)(26 * u) - b, r2 = (int)(18 * u) - b;
  g.fillCircle(X(55), Y(30), r1, c);
  g.fillCircle(X(28), Y(42), r2, c);
  g.fillCircle(X(78), Y(42), r2, c);
  g.fillRect(X(28), Y(42), X(78) - X(28), Y(60) - Y(42) - b, c);
}
inline void cloud(Adafruit_GFX& g, int x, int y, float u, int t, uint16_t fg, uint16_t bg) {
  cloudFill(g, x, y, u, 0, fg);
  cloudFill(g, x, y, u, t, bg);
}
inline void thickLine(Adafruit_GFX& g, int x0, int y0, int x1, int y1, int t, uint16_t c) {
  for (int i = 0; i < t; i++) g.drawLine(x0 + i, y0, x1 + i, y1, c);
}
inline void sun(Adafruit_GFX& g, int cx, int cy, int r, int t, uint16_t fg, uint16_t bg) {
  for (int k = 0; k < 8; k++) {
    float a = k * PI / 4;
    int x0 = cx + (int)((r + t + 2) * cosf(a)), y0 = cy + (int)((r + t + 2) * sinf(a));
    int x1 = cx + (int)((r + t + 2 + r * 0.55f) * cosf(a)), y1 = cy + (int)((r + t + 2 + r * 0.55f) * sinf(a));
    thickLine(g, x0, y0, x1, y1, t, fg);
  }
  g.fillCircle(cx, cy, r, fg);
  g.fillCircle(cx, cy, r - t, bg);
}
// 斜线雨：n 条，长度 len（单位 u）
inline void rainLines(Adafruit_GFX& g, int x, int y, float u, int t, float len, uint16_t c) {
  const float xs[3] = {36, 56, 76};
  for (float sx : xs) {
    int x0 = x + (int)(sx * u), y0 = y + (int)(68 * u);
    thickLine(g, x0, y0, x0 - (int)(len * 0.4f * u), y0 + (int)(len * u), t, c);
  }
}

inline void drawWIcon(Adafruit_GFX& g, WIcon ic, int x, int y, int w, uint16_t fg, uint16_t bg) {
  float u = w / 100.0f;
  int t = w >= 60 ? 4 : 2;
  switch (ic) {
    case WI_SUN:
      sun(g, x + (int)(50 * u), y + (int)(44 * u), (int)(22 * u), t, fg, bg);
      break;
    case WI_PARTLY:
      sun(g, x + (int)(34 * u), y + (int)(26 * u), (int)(15 * u), t, fg, bg);
      cloud(g, x + (int)(12 * u), y + (int)(26 * u), u * 0.88f, t, fg, bg);
      break;
    case WI_CLOUD:
      cloud(g, x, y + (int)(12 * u), u, t, fg, bg);
      break;
    case WI_OVERCAST:   // 后面一朵小云 + 前面一朵大云
      cloud(g, x + (int)(24 * u), y + (int)(4 * u), u * 0.72f, t, fg, bg);
      cloud(g, x, y + (int)(18 * u), u * 0.92f, t, fg, bg);
      break;
    case WI_FOG:
      cloud(g, x, y, u, t, fg, bg);
      for (int i = 0; i < 3; i++) {
        int yy = y + (int)((70 + i * 10) * u);
        g.fillRect(x + (int)((10 + i * 8) * u), yy, (int)((80 - i * 16) * u), t, fg);
      }
      break;
    case WI_DRIZZLE:
      cloud(g, x, y, u, t, fg, bg);
      rainLines(g, x, y, u, t > 2 ? t - 1 : 1, 12, fg);
      break;
    case WI_RAIN:
      cloud(g, x, y, u, t, fg, bg);
      rainLines(g, x, y, u, t, 22, fg);
      break;
    case WI_SHOWER:
      sun(g, x + (int)(76 * u), y + (int)(16 * u), (int)(11 * u), t > 2 ? t - 1 : 1, fg, bg);
      cloud(g, x, y + (int)(4 * u), u * 0.9f, t, fg, bg);
      rainLines(g, x - (int)(4 * u), y, u, t, 18, fg);
      break;
    case WI_SNOW:
    case WI_SLEET: {
      cloud(g, x, y, u, t, fg, bg);
      const float xs[3] = {32, 54, 76};
      for (int i = 0; i < 3; i++) {
        int cx = x + (int)(xs[i] * u), cy = y + (int)((i == 1 ? 84 : 76) * u);
        int r = (int)(6 * u) + 1;
        if (ic == WI_SLEET && i == 1) { thickLine(g, cx + r, cy - r, cx - r / 2, cy + r, t, fg); continue; }
        g.drawLine(cx - r, cy, cx + r, cy, fg);
        g.drawLine(cx, cy - r, cx, cy + r, fg);
        g.drawLine(cx - r * 7 / 10, cy - r * 7 / 10, cx + r * 7 / 10, cy + r * 7 / 10, fg);
        g.drawLine(cx - r * 7 / 10, cy + r * 7 / 10, cx + r * 7 / 10, cy - r * 7 / 10, fg);
      }
      break;
    }
    case WI_THUNDER: {
      cloud(g, x, y, u, t, fg, bg);
      int bx = x + (int)(52 * u), by = y + (int)(62 * u);
      g.fillTriangle(bx, by, bx - (int)(12 * u), by + (int)(16 * u), bx + (int)(2 * u), by + (int)(14 * u), fg);
      g.fillTriangle(bx + (int)(4 * u), by + (int)(12 * u), bx - (int)(6 * u), by + (int)(14 * u), bx - (int)(8 * u), by + (int)(30 * u), fg);
      break;
    }
  }
}
