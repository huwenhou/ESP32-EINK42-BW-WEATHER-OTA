// =====================================================================
//  天气看板配置
// =====================================================================
#pragma once

// 固件发布地址（GitHub Pages）
#define FW_BASE_URL "https://huwenhou.github.io/ESP32-EINK42-BW-WEATHER-OTA"

// 城市（顶栏显示名 + 经纬度）
#define CITY_NAME "上海"
#define CITY_LAT  "31.23"
#define CITY_LON  "121.47"

// 天气刷新间隔（分钟）
#define WEATHER_MINUTES 30

// 配网热点名
#define AP_NAME "Robbie's Weather Board"

// 屏幕驱动：0 = 开机自动识别；1 = UC8176 系列（GxEPD2_420）；2 = SSD1683 系列（GDEY042T81）
#define FORCE_PANEL 2   // 已确认这块屏是 SSD1683，固定使用，避免开机误判
