// =====================================================================
//  HINK-E042A03-A1 驱动（4.2" 400x300 黑白，双 SSD1608）v1.0
//  · 主片 CS#（排线12脚）管物理左半 x=0..199
//  · 从片 CS1#（排线1脚）管物理右半 x=200..399；微雪 ESP32 板上该脚常选中，
//    所以写右半时把 CS# 拉高，只有从片收到数据
//  · 每片 RAM 原点在本半屏左上角，X 向右、Y 向下
//  · 波形：原厂例程 LUT（芯片 OTP 为空，必须由 MCU 写入），一次全刷约 6 秒
// =====================================================================
#pragma once
#include <Adafruit_GFX.h>
#include <SPI.h>

class HINK042 : public Adafruit_GFX {
public:
  static const int16_t W = 400, H = 300;
  static const uint16_t BLACK = 0x0000, WHITE = 0xFFFF;

  HINK042(int8_t cs, int8_t dc, int8_t rst, int8_t busy)
    : Adafruit_GFX(W, H), _cs(cs), _dc(dc), _rst(rst), _busy(busy),
      _spi(4000000, MSBFIRST, SPI_MODE0) {}

  void begin(int8_t sck, int8_t mosi) {
    pinMode(_busy, INPUT);
    pinMode(_rst, OUTPUT);
    pinMode(_dc, OUTPUT);
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);
    digitalWrite(_dc, HIGH);
    digitalWrite(_rst, HIGH);
    SPI.begin(sck, -1, mosi, -1);
    fillScreen(WHITE);
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    uint8_t& b = _buf[y * (W / 8) + (x >> 3)];
    uint8_t m = 0x80 >> (x & 7);
    if (color == WHITE) b |= m; else b &= ~m;
  }
  void fillScreen(uint16_t color) override { memset(_buf, color == WHITE ? 0xFF : 0x00, sizeof(_buf)); }

  // 把缓冲区整屏刷到屏上（约 6 秒），刷完进入深度睡眠
  bool display() {
    SPI.beginTransaction(_spi);
    _init();
    _writeHalf(true);    // 主片：左半（CS# 低，两片都收到）
    _writeHalf(false);   // 从片：右半（CS# 高，只有从片收到，覆盖上一步）
    _csLow(); _cmd(0x22); _dat(0xC7); _cmd(0x20); _csHigh();
    bool ok = _waitBusy("refresh", 20000);
    _csLow(); _cmd(0x10); _dat(0x01); _csHigh();   // 深度睡眠（下次 display 会硬件复位唤醒）
    SPI.endTransaction();
    return ok;
  }

private:
  int8_t _cs, _dc, _rst, _busy;
  SPISettings _spi;
  uint8_t _buf[W / 8 * H];

  void _csLow()  { digitalWrite(_cs, LOW); }
  void _csHigh() { digitalWrite(_cs, HIGH); }
  void _cmd(uint8_t c) { digitalWrite(_dc, LOW); SPI.transfer(c); digitalWrite(_dc, HIGH); }
  void _dat(uint8_t d) { SPI.transfer(d); }

  bool _waitBusy(const char* tag, uint32_t timeoutMs) {
    uint32_t t0 = millis();
    delay(2);
    while (digitalRead(_busy) == HIGH) {
      if (millis() - t0 > timeoutMs) { Serial.printf("[epd] %s TIMEOUT\n", tag); return false; }
      delay(1);
    }
    Serial.printf("[epd] %s %lu ms\n", tag, millis() - t0);
    return true;
  }

  void _init() {
    static const uint8_t LUT[30] = {
      0x66, 0xa6, 0x6a, 0x66, 0x55, 0x99, 0xaa, 0x66,
      0x55, 0x95, 0xaa, 0xaa, 0x59, 0x55, 0xaa, 0x55,
      0x88, 0x11, 0x11, 0x88,
      0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0x8f, 0x8f };
    digitalWrite(_rst, LOW);  delay(10);
    digitalWrite(_rst, HIGH); delay(10);
    _csLow(); _cmd(0x12); _csHigh();               // 软复位
    _waitBusy("reset", 2000);
    _csLow();
    _cmd(0x01); _dat(0x2B); _dat(0x01); _dat(0x00);  // 300 行
    _cmd(0x3A); _dat(0x16);
    _cmd(0x3B); _dat(0x08);
    _cmd(0x21); _dat(0x83);
    _cmd(0x3C); _dat(0x33);                        // 边框
    _cmd(0x11); _dat(0x03);                        // X+ Y+
    _cmd(0x44); _dat(0x00); _dat(0x18);            // X 0..24（200 像素）
    _cmd(0x45); _dat(0x00); _dat(0x00); _dat(0x2B); _dat(0x01);  // Y 0..299
    _cmd(0x2C); _dat(0xB9);                        // VCOM
    _cmd(0x32); for (int i = 0; i < 30; i++) _dat(LUT[i]);
    _csHigh();
  }

  void _writeHalf(bool master) {
    if (master) _csLow(); else _csHigh();
    _cmd(0x4E); _dat(0x00);
    _cmd(0x4F); _dat(0x00); _dat(0x00);
    _cmd(0x24);
    const int off = master ? 0 : W / 16;           // 主片=左半，从片=右半（从第 25 字节列开始）
    for (int y = 0; y < H; y++) {
      const uint8_t* row = _buf + y * (W / 8) + off;
      for (int bx = 0; bx < W / 16; bx++) _dat(row[bx]);
    }
    _csHigh();
  }
};
