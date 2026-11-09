// =====================================================================
//  天气看板配置（老款屏 HINK-E042A03-A1）
// =====================================================================
#pragma once

// OTA：老款屏驱动与新款不同，绝不能从新款固件频道升级（会刷成黑屏），默认关闭
#define ENABLE_OTA 0
#define FW_BASE_URL     "https://io.huwenhou.com/ESP32-EINK42-BW-WEATHER-OLD-OTA"
#define FW_FALLBACK_URL "https://huwenhou.github.io/ESP32-EINK42-BW-WEATHER-OLD-OTA"

// 城市（顶栏显示名 + 经纬度）
#define CITY_NAME "上海"
#define CITY_LAT  "31.23"
#define CITY_LON  "121.47"

// 天气刷新间隔（分钟）
#define WEATHER_MINUTES 60   // 固定为整点刷新（深度睡眠），此项仅作说明

// 配网热点名
#define AP_NAME "Robbie's Weather Board"
