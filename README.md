# 4.2" 黑白墨水屏 · 天气看板

ESP32 驱动板 + 4.2 寸黑白屏。左侧显示当前天气（图标、温度、体感、湿度、风力、日出日落），右侧显示未来 6 天预报和温度区间条。

- 数据：Open-Meteo，免费、无需密钥，每 30 分钟更新（`config.h` 可改城市和频率）
- 屏幕驱动开机自动识别（UC8176 / SSD1683），结果显示在开机画面左下角
- 每 10 分钟检查一次新固件，自动 OTA

## 刷机
电脑 Chrome 打开 https://huwenhou.github.io/ESP32-EINK42-BW-WEATHER-OTA ，点安装，选驱动板串口。
之后用手机连热点 **Robbie's Weather Board** 配 Wi-Fi。

## 目录
```
firmware/weather/weather.ino  主程序（取数、排版、OTA）
firmware/weather/icons.h      天气图标与天气代码对照
firmware/weather/config.h     城市、刷新频率、热点名、驱动选择
web/index.html                网页刷机页
.github/workflows/build.yml   生成数字字体 → 编译 → 发布到 GitHub Pages
```

## 本地编译
大号数字字体 `Barlow_Bold33pt7b.h` 由 CI 用 fontconvert 生成，不在仓库里。
Partition Scheme 选 **Minimal SPIFFS (1.9MB APP with OTA)**。
