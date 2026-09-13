#include <Arduino.h>
#include <U8g2lib.h>

// ESP32-C3 0.42" OLED board: SSD1306 72x40 on I2C, SCL=GPIO6, SDA=GPIO5
static U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5);

static const int BUTTON_PIN = 9;  // BOOT button, active low
static const int LED_PIN = 8;

static const int SCREEN_W = 72;
static const int SCREEN_H = 40;

struct Date {
  const char *weekday;
  const char *day;
};

static const Date DATES[] = {{"Fr", "26.09."}, {"Do", "02.10."}, {"Fr", "10.10."}};
static const size_t DATE_COUNT = sizeof(DATES) / sizeof(DATES[0]);

// Carousel: one card centred, neighbours peek in at both edges
static const int CARD_W = 48;
static const int CARD_H = 28;
static const int CARD_Y = 1;
static const int CARD_X = (SCREEN_W - CARD_W) / 2;
static const int CARD_PITCH = CARD_W + 6;
static const int SLIDE_FRAMES = 12;

static size_t selected = 0;
static bool buttonWasDown = false;

// Old CRT power-off in reverse: dot -> horizontal line -> full white -> black
static void playCrtOn() {
  for (int w = 2; w <= SCREEN_W; w += 4) {
    u8g2.clearBuffer();
    u8g2.drawBox((SCREEN_W - w) / 2, SCREEN_H / 2 - 1, w, 2);
    u8g2.sendBuffer();
    delay(15);
  }
  for (int h = 2; h <= SCREEN_H; h += 2) {
    u8g2.clearBuffer();
    u8g2.drawBox(0, (SCREEN_H - h) / 2, SCREEN_W, h);
    u8g2.sendBuffer();
    delay(15);
  }
  delay(300);
}

static const int BAR_STEPS = 30;

// Small black mug icon, top-left at (x, y), 17x16 px
static void drawMugIcon(int x, int y) {
  u8g2.drawRFrame(x + 11, y + 6, 6, 8, 2);  // handle
  u8g2.setDrawColor(1);
  u8g2.drawBox(x, y + 3, 12, 13);  // erase handle inside the body
  u8g2.setDrawColor(0);
  u8g2.drawRFrame(x, y + 3, 12, 13, 1);  // body
  for (int py = y + 9; py < y + 15; py++) {  // dithered beer
    for (int px = x + 2; px < x + 10; px++) {
      if ((px + py) % 2 == 0) {
        u8g2.drawPixel(px, py);
      }
    }
  }
  u8g2.drawDisc(x + 2, y + 3, 2);  // foam
  u8g2.drawDisc(x + 6, y + 2, 2);
  u8g2.drawDisc(x + 10, y + 3, 2);
}

// Everything in black on the white screen left by the CRT flash
static void drawLoadingScreen(int progress) {
  static const int BAR_X = 6;
  static const int BAR_Y = 34;
  static const int BAR_W = SCREEN_W - 2 * BAR_X;
  static const int BAR_H = 3;
  u8g2.clearBuffer();
  u8g2.drawBox(0, 0, SCREEN_W, SCREEN_H);
  u8g2.setDrawColor(0);
  drawMugIcon(28, 1);
  u8g2.drawStr(6, 20, "cheers.exe");
  int done = BAR_W * progress / BAR_STEPS;
  u8g2.drawBox(BAR_X, BAR_Y, done, BAR_H);
  for (int y = BAR_Y; y < BAR_Y + BAR_H; y++) {  // gray remainder
    for (int x = BAR_X + done; x < BAR_X + BAR_W; x++) {
      if ((x + y) % 2 == 0) {
        u8g2.drawPixel(x, y);
      }
    }
  }
  u8g2.setDrawColor(1);
}

// Black beer with a wavy surface and a dotted foam band, drawn over the buffer
static void drawBeerOver(int level, int phase) {
  static const int FOAM_H = 5;
  u8g2.setDrawColor(0);
  for (int x = 0; x < SCREEN_W; x++) {
    int surface = level + lroundf(2.0f * sinf((x + phase) / 5.0f));
    for (int y = max(surface, 0); y < min(surface + FOAM_H, SCREEN_H); y++) {
      if ((x + 2 * y) % 3 == 0) {
        u8g2.drawPixel(x, y);
      }
    }
    int beerTop = constrain(surface + FOAM_H, 0, SCREEN_H);
    u8g2.drawVLine(x, beerTop, SCREEN_H - beerTop);
  }
  u8g2.setDrawColor(1);
}

static void playLoading() {
  for (int progress = 0; progress <= BAR_STEPS; progress++) {
    drawLoadingScreen(progress);
    u8g2.sendBuffer();
    delay(60);
  }
  delay(300);
  int phase = 0;
  for (int level = SCREEN_H + 2; level >= -8; level--) {
    drawLoadingScreen(BAR_STEPS);
    drawBeerOver(level, phase++);
    u8g2.sendBuffer();
    delay(40);
  }
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  delay(400);
}

static void drawCentered(int cardX, int y, const char *text) {
  u8g2.drawStr(cardX + (CARD_W - u8g2.getStrWidth(text)) / 2, y, text);
}

static void drawCard(int x, const Date &date, bool active) {
  u8g2.drawRFrame(x, CARD_Y, CARD_W, CARD_H, 3);
  if (active) {
    u8g2.drawRFrame(x + 1, CARD_Y + 1, CARD_W - 2, CARD_H - 2, 2);
  }
  u8g2.setFont(u8g2_font_6x10_tf);
  drawCentered(x, CARD_Y + 4, date.weekday);
  u8g2.setFont(u8g2_font_7x13B_tf);
  drawCentered(x, CARD_Y + 13, date.day);
}

// offset shifts the whole strip left while sliding to the next card
static void drawCarousel(size_t center, int offset) {
  u8g2.clearBuffer();
  for (int k = -1; k <= 2; k++) {
    size_t index = (center + DATE_COUNT + k) % DATE_COUNT;
    drawCard(CARD_X + k * CARD_PITCH + offset, DATES[index], k == 0 && offset == 0);
  }
  int dotsX = SCREEN_W / 2 - (DATE_COUNT - 1) * 3;
  for (size_t i = 0; i < DATE_COUNT; i++) {
    int x = dotsX + i * 6;
    if (i == center) {
      u8g2.drawDisc(x, 36, 1);
    } else {
      u8g2.drawPixel(x, 36);
    }
  }
  u8g2.sendBuffer();
}

static void slideToNext() {
  for (int frame = 1; frame <= SLIDE_FRAMES; frame++) {
    float t = static_cast<float>(frame) / SLIDE_FRAMES;
    float eased = 1.0f - (1.0f - t) * (1.0f - t);
    drawCarousel(selected, -lroundf(eased * CARD_PITCH));
    delay(16);
  }
  selected = (selected + 1) % DATE_COUNT;
  drawCarousel(selected, 0);
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // Blue LED is active-low; keep it off
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  Serial.begin(115200);
  u8g2.begin();
  u8g2.setContrast(255);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setFontPosTop();
  u8g2.setFontMode(1);

  playCrtOn();
  playLoading();
  drawCarousel(selected, 0);
}

void loop() {
  bool buttonDown = digitalRead(BUTTON_PIN) == LOW;
  bool pressed = buttonDown && !buttonWasDown;
  buttonWasDown = buttonDown;

  if (pressed) {
    slideToNext();
    Serial.printf("selected %s %s\n", DATES[selected].weekday, DATES[selected].day);
  }
  delay(10);
}
