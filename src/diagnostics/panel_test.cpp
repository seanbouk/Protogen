// Panel diagnostics firmware - build with:  pio run -e panel-test
// Not part of the mask firmware (compiles to nothing unless LF_PANEL_TEST is defined).
//
// Shows test patterns on the 2x 64x32 HUB75 panels and lets you change the display driver
// settings live, so colour / gradation / flicker questions can be answered by looking.
//
//   UP button   = next pattern            DOWN button = next clock speed (reboots)
//   Serial (115200), one character, no Enter needed:
//     n / p  next / previous pattern       c  next clock speed      r  next min refresh rate
//     d      next colour depth (see note)  + / -  brightness        l  latch blanking 1..4
//     k      toggle clock phase            s  print status          0-9 jump to pattern
//     a / b / w  patterns 10 / 11 / 12     x  reset to known-good defaults
//   Driver settings live in RTC memory and survive the quick reboot needed to apply them.
//
// FINDINGS (2026-11-10, MatrixPortal S3 + 2x 64x32 FM6126A-configured panels, USB-C from a desktop)
//   - Colour order is standard RGB (the upstream "waveshare" env's green/blue swap is wrong for these).
//   - Physical layout: chain panel 1 is on the right as you face the panels; x runs left to right overall.
//   - Rainbow banding / non-monotonic ramps: HUB75 lib 3.1.0 picks its CIE brightness table at COMPILE time
//     (PIXEL_COLOR_DEPTH_BITS, default 8) but depth is set at RUN time. Runtime 7-bit with the 8-bit table drops
//     the table's top bit, so brightness wraps near input ~190. Fixed by building with -DPIXEL_COLOR_DEPTH_BITS=7.
//     => The 'd' command only gives correct colours at the compiled depth; other depths reproduce that bug.
//   - Known good: 16MHz, min refresh 120Hz, 7-bit, latch 1, clkphase false -> 205Hz actual, smooth, no flicker.
//     8MHz flickers (55Hz). 8-bit depth is smooth but ~57Hz at 16MHz -> visible flicker.
//   - Full white at full brightness browns out on desktop USB power (page 3). Needs a power budget / supply.
//   - Not clock-related: 8MHz showed the same banding as 20MHz; column/row mapping verified (pages 10-12).

#ifdef LF_PANEL_TEST

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

namespace
{
constexpr int PANEL_W = 64, PANEL_H = 32, CHAIN = 2;
constexpr int W = PANEL_W * CHAIN, H = PANEL_H;
constexpr int BTN_UP = 6, BTN_DOWN = 7;

const HUB75_I2S_CFG::clk_speed CLOCKS[] = {HUB75_I2S_CFG::HZ_8M, HUB75_I2S_CFG::HZ_10M, HUB75_I2S_CFG::HZ_15M,
                                           HUB75_I2S_CFG::HZ_16M, HUB75_I2S_CFG::HZ_20M};
const char *CLOCK_NAMES[] = {"8MHz", "10MHz", "15MHz", "16MHz", "20MHz"};
const uint16_t REFRESH[] = {40, 60, 90, 120, 200};
constexpr int N_CLOCKS = 5, N_REFRESH = 5;

struct Settings
{
  uint32_t magic;
  uint8_t clock, refresh, depth, latch, brightness, page;
  bool clkphase;
};
constexpr uint32_t MAGIC = 0x50414E4C; // "PANL"
RTC_NOINIT_ATTR Settings cfg;

MatrixPanel_I2S_DMA *panel = nullptr;
const char *PAGE_NAMES[] = {"colour bars", "channel ramps", "button hues + hue sweep", "full white",
                            "full R/G/B cycle", "moving row", "row bands", "checkerboard", "grey steps",
                            "both panels identical", "sweep top+bottom (565)", "sweep top 565 / bottom RGB888",
                            "walking column"};
constexpr int N_PAGES = sizeof(PAGE_NAMES) / sizeof(PAGE_NAMES[0]);

void defaults()
{
  // 16MHz: stable and flicker-free on these panels (20MHz is what the shipped firmware ran).
  cfg = {MAGIC, 3 /*16MHz*/, 3 /*120Hz*/, PIXEL_COLOR_DEPTH_BITS, 1, 255, 0, false};
}

void printStatus()
{
  Serial.printf("\n[panel-test] page %d: %s\n", cfg.page, PAGE_NAMES[cfg.page]);
  Serial.printf("  clock %s  min refresh %uHz  depth %u bits (table compiled for %d)  latch %u  clkphase %s  brightness %u\n",
                CLOCK_NAMES[cfg.clock], REFRESH[cfg.refresh], cfg.depth, PIXEL_COLOR_DEPTH_BITS, cfg.latch,
                cfg.clkphase ? "true" : "false", cfg.brightness);
  if (panel)
  {
    Serial.printf("  calculated refresh: %d Hz\n", panel->calculated_refresh_rate);
  }
}

void applyAndReboot()
{
  printStatus();
  Serial.println("  rebooting to apply...");
  Serial.flush();
  delay(50);
  ESP.restart();
}

uint16_t c565(uint8_t r, uint8_t g, uint8_t b) { return panel->color565(r, g, b); }

void hsv(float h, uint8_t &r, uint8_t &g, uint8_t &b)
{
  h = fmodf(h, 360.0f);
  const float x = 1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f);
  float rf = 0, gf = 0, bf = 0;
  switch (int(h / 60.0f))
  {
  case 0: rf = 1; gf = x; break;
  case 1: rf = x; gf = 1; break;
  case 2: gf = 1; bf = x; break;
  case 3: gf = x; bf = 1; break;
  case 4: rf = x; bf = 1; break;
  default: rf = 1; bf = x; break;
  }
  r = lrintf(rf * 255); g = lrintf(gf * 255); b = lrintf(bf * 255);
}

void label(int x, int y, const char *t, uint16_t colour)
{
  panel->setTextSize(1);
  panel->setTextWrap(false);
  panel->setTextColor(colour);
  panel->setCursor(x, y);
  panel->print(t);
}

// Static patterns are drawn once; animated ones are redrawn each frame.
void drawPage(uint32_t frame)
{
  switch (cfg.page)
  {
  case 0: // colour bars: top = full, bottom = half intensity. R G B C M Y W K on each panel.
  {
    const uint8_t cols[8][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {0, 255, 255},
                                {255, 0, 255}, {255, 255, 0}, {255, 255, 255}, {0, 0, 0}};
    const char *names = "RGBCMYWK";
    for (int p = 0; p < CHAIN; ++p)
      for (int i = 0; i < 8; ++i)
      {
        const int x = p * PANEL_W + i * 8;
        panel->fillRect(x, 0, 8, 16, c565(cols[i][0], cols[i][1], cols[i][2]));
        panel->fillRect(x, 16, 8, 16, c565(cols[i][0] / 2, cols[i][1] / 2, cols[i][2] / 2));
        char s[2] = {names[i], 0};
        label(x + 1, 4, s, i == 7 ? c565(255, 255, 255) : c565(0, 0, 0)); // white letter on the black bar
      }
    break;
  }
  case 1: // ramps 0..255 across the full width: R, G, B, W bands of 8 rows
    for (int x = 0; x < W; ++x)
    {
      const uint8_t v = (x * 255) / (W - 1);
      panel->drawFastVLine(x, 0, 8, c565(v, 0, 0));
      panel->drawFastVLine(x, 8, 8, c565(0, v, 0));
      panel->drawFastVLine(x, 16, 8, c565(0, 0, v));
      panel->drawFastVLine(x, 24, 8, c565(v, v, v));
    }
    break;
  case 2: // top: the 24 DOWN-button hues (15 deg steps); bottom: continuous hue sweep
    for (int x = 0; x < W; ++x)
    {
      uint8_t r, g, b;
      hsv((x * 24 / W) * 15.0f, r, g, b);
      panel->drawFastVLine(x, 0, 15, c565(r, g, b));
      hsv(x * 360.0f / W, r, g, b);
      panel->drawFastVLine(x, 17, 15, c565(r, g, b));
    }
    break;
  case 3: // full white - power stress
    panel->fillScreen(c565(255, 255, 255));
    break;
  case 4: // full red / green / blue, 2 s each
  {
    const int k = (frame / 100) % 3;
    panel->fillScreen(c565(k == 0 ? 255 : 0, k == 1 ? 255 : 0, k == 2 ? 255 : 0));
    break;
  }
  case 5: // a single white row walking top to bottom, row number shown
  {
    const int row = (frame / 25) % H;
    panel->fillScreen(0);
    panel->drawFastHLine(0, row, W, c565(255, 255, 255));
    char s[8];
    snprintf(s, sizeof(s), "%d", row);
    label(row < 16 ? 2 : 2, row < 16 ? 22 : 2, s, c565(255, 0, 0));
    label(PANEL_W + 2, row < 16 ? 22 : 2, s, c565(255, 0, 0));
    break;
  }
  case 6: // 4-row bands alternating colours - shows which rows misbehave
    for (int y = 0; y < H; ++y)
    {
      const int band = y / 4;
      const uint16_t col = (band % 2) ? c565(255, 255, 255) : c565(0, 64, 255);
      panel->drawFastHLine(0, y, W, col);
    }
    break;
  case 7: // 1-pixel checkerboard - ghosting / scan problems show up as smearing
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x)
        panel->drawPixel(x, y, ((x + y) & 1) ? c565(255, 255, 255) : 0);
    break;
  case 8: // 16 grey steps - is the low end distinguishable?
    for (int i = 0; i < 16; ++i)
    {
      const uint8_t v = i * 17;
      panel->fillRect(i * 8, 0, 8, H, c565(v, v, v));
    }
    break;
  case 9: // identical test card on both panels, with panel number
    for (int p = 0; p < CHAIN; ++p)
    {
      const int x0 = p * PANEL_W;
      panel->drawRect(x0, 0, PANEL_W, H, c565(255, 255, 255));
      panel->drawLine(x0, 0, x0 + PANEL_W - 1, H - 1, c565(255, 0, 0));
      panel->drawLine(x0, H - 1, x0 + PANEL_W - 1, 0, c565(0, 255, 0));
      char s[12];
      snprintf(s, sizeof(s), "PANEL %d", p + 1);
      label(x0 + 12, 12, s, c565(0, 128, 255));
    }
    break;
  case 10: // identical continuous sweep in the top half and the bottom half
    for (int x = 0; x < W; ++x)
    {
      uint8_t r, g, b;
      hsv(x * 360.0f / W, r, g, b);
      panel->drawFastVLine(x, 0, 15, c565(r, g, b));
      panel->drawFastVLine(x, 17, 15, c565(r, g, b));
    }
    break;
  case 11: // top: sweep via 16-bit 565 colour; bottom: same sweep via full 24-bit pixel writes
    for (int x = 0; x < W; ++x)
    {
      uint8_t r, g, b;
      hsv(x * 360.0f / W, r, g, b);
      panel->drawFastVLine(x, 0, 15, c565(r, g, b));
      for (int y = 17; y < H; ++y)
        panel->drawPixelRGB888(x, y, r, g, b);
    }
    break;
  case 12: // one white column walking left to right, column number shown
  {
    const int col = (frame / 15) % W;
    panel->fillScreen(0);
    panel->drawFastVLine(col, 0, H, c565(255, 255, 255));
    char s[8];
    snprintf(s, sizeof(s), "%d", col);
    label(col < 64 ? 70 : 6, 12, s, c565(255, 0, 0));
    break;
  }
  }
}

bool isAnimated() { return cfg.page == 4 || cfg.page == 5 || cfg.page == 12; }

void showPage()
{
  panel->clearScreen();
  drawPage(0);
  printStatus();
}

struct Btn
{
  explicit Btn(int p) : pin(p) {}
  int pin;
  bool last = true;
  unsigned long changed = 0;
  bool pressed()
  {
    const bool now = digitalRead(pin);
    if (now != last && millis() - changed > 40)
    {
      changed = millis();
      last = now;
      return !now; // active low
    }
    return false;
  }
};
Btn up{BTN_UP}, down{BTN_DOWN};
} // namespace

void setup()
{
  Serial.begin(115200);
  if (cfg.magic != MAGIC || cfg.clock >= N_CLOCKS || cfg.refresh >= N_REFRESH || cfg.page >= N_PAGES)
  {
    defaults();
  }
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  // MatrixPortal S3 standard pin order (R1 G1 B1 R2 G2 B2 A B C D E LAT OE CLK)
  HUB75_I2S_CFG::i2s_pins pins = {42, 41, 40, 38, 39, 37, 45, 36, 48, 35, 21, 47, 14, 2};
  HUB75_I2S_CFG mx(PANEL_W, PANEL_H, CHAIN, pins);
  mx.driver = HUB75_I2S_CFG::FM6126A;
  mx.i2sspeed = CLOCKS[cfg.clock];
  mx.min_refresh_rate = REFRESH[cfg.refresh];
  mx.latch_blanking = cfg.latch;
  mx.clkphase = cfg.clkphase;
  mx.double_buff = false;
  mx.setPixelColorDepthBits(cfg.depth);
  panel = new MatrixPanel_I2S_DMA(mx);
  if (!panel->begin())
  {
    Serial.println("[panel-test] panel begin() failed");
  }
  panel->setBrightness8(cfg.brightness);
  delay(300);
  Serial.println("\n[panel-test] ready. Commands: n p c r d + - l k s 0-9");
  showPage();
}

void loop()
{
  static uint32_t frame = 0;
  if (up.pressed())
  {
    cfg.page = (cfg.page + 1) % N_PAGES;
    showPage();
  }
  if (down.pressed())
  {
    cfg.clock = (cfg.clock + 1) % N_CLOCKS;
    applyAndReboot();
  }
  while (Serial.available())
  {
    const char c = Serial.read();
    switch (c)
    {
    case 'n': cfg.page = (cfg.page + 1) % N_PAGES; showPage(); break;
    case 'p': cfg.page = (cfg.page + N_PAGES - 1) % N_PAGES; showPage(); break;
    case 'c': cfg.clock = (cfg.clock + 1) % N_CLOCKS; applyAndReboot(); break;
    case 'r': cfg.refresh = (cfg.refresh + 1) % N_REFRESH; applyAndReboot(); break;
    case 'd':
      cfg.depth = cfg.depth >= 8 ? 4 : cfg.depth + 1;
      if (cfg.depth != PIXEL_COLOR_DEPTH_BITS)
        Serial.printf("  NOTE: brightness table is compiled for %d bits - colours will be WRONG at %u bits\n",
                      PIXEL_COLOR_DEPTH_BITS, cfg.depth);
      applyAndReboot();
      break;
    case 'l': cfg.latch = cfg.latch >= 4 ? 1 : cfg.latch + 1; applyAndReboot(); break;
    case 'k': cfg.clkphase = !cfg.clkphase; applyAndReboot(); break;
    case '+': cfg.brightness = min(255, cfg.brightness + 32); panel->setBrightness8(cfg.brightness); printStatus(); break;
    case '-': cfg.brightness = max(8, cfg.brightness - 32); panel->setBrightness8(cfg.brightness); printStatus(); break;
    case 's': printStatus(); break;
    case 'a': cfg.page = 10; showPage(); break;
    case 'b': cfg.page = 11; showPage(); break;
    case 'w': cfg.page = 12; showPage(); break;
    case 'x': defaults(); applyAndReboot(); break;
    default:
      if (c >= '0' && c <= '9' && c - '0' < N_PAGES)
      {
        cfg.page = c - '0';
        showPage();
      }
    }
  }
  if (isAnimated())
  {
    drawPage(frame);
  }
  ++frame;
  delay(20);
}

#endif // LF_PANEL_TEST
