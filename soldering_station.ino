#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <math.h>

// --- PINY SPI ---
#define MOSI_PIN 5
#define MISO_PIN 6
#define SCK_PIN  7

// --- PINY EKRANU I DOTYKU ---
#define TFT_CS   10
#define TFT_RST  11
#define TFT_DC   12
#define TOUCH_CS 13
#define TOUCH_IRQ 14

// --- PINY PERYFERYJNE STACJI ---
#define STEP_PIN 15
#define DIR_PIN  16
#define EN_PIN   17
#define SERVO_PIN 8
#define LIMIT_1 1
#define LIMIT_2 2
#define CONTINUITY_PIN 9
#define RGB_LED_PIN 48

// --- KLASA KONFIGURACYJNA LOVYANGFX ---
class LGFX_STACJA : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Touch_XPT2046 _touch_instance;

public:
  LGFX_STACJA() {
    auto cfg = _bus_instance.config();
    cfg.spi_host = (spi_host_device_t)SPI3_HOST;
    cfg.spi_mode = 0;
    cfg.freq_write = 20000000;
    cfg.freq_read  = 2500000;
    cfg.pin_sclk = SCK_PIN;
    cfg.pin_mosi = MOSI_PIN;
    cfg.pin_miso = MISO_PIN;
    cfg.pin_dc   = TFT_DC;
    _bus_instance.config(cfg);
    _panel_instance.setBus(&_bus_instance);

    auto p_cfg = _panel_instance.config();
    p_cfg.pin_cs           = TFT_CS;
    p_cfg.pin_rst          = TFT_RST;
    p_cfg.panel_width      = 320;
    p_cfg.panel_height     = 480;
    p_cfg.readable         = false;
    _panel_instance.config(p_cfg);

    auto t_cfg = _touch_instance.config();
    t_cfg.spi_host = (spi_host_device_t)SPI3_HOST;
    t_cfg.bus_shared = true;
    t_cfg.pin_cs     = TOUCH_CS;
    t_cfg.freq       = 2500000;
    t_cfg.x_min      = 300;
    t_cfg.x_max      = 3800;
    t_cfg.y_min      = 200;
    t_cfg.y_max      = 3700;

    _touch_instance.config(t_cfg);
    _panel_instance.setTouch(&_touch_instance);
    setPanel(&_panel_instance);
  }
};

LGFX_STACJA lcd;

// --- ZMIENNE GLOBALNE ---
unsigned long lastStepTime = 0;
const int stepDelayMicros = 4000;
bool motorRunning = false;
bool isJogging = false;

bool isReversing = false;
long stepsRemaining = 0;
const long STEPS_PER_REV = 800;

int currentServoAngle = 135;
int targetServoAngle = 135;
unsigned long lastServoTime = 0;
const int servoSpeedMs = 10;

// GUI Multimetru i Ustawienia systemowe
bool modeOhm = false;
int lastAngle = 270;
int lastAdc = -1;
bool rgbEnabled = true;
bool isDarkMode = true; // Zmienna trybu jasny/ciemny
bool screenAsleep = false; // Zmienna trybu uśpienia ekranu

// --- OPUSZCZONE WSPÓŁRZĘDNE UKŁADU ---
const int cx = 160;
const int cy = 210;
const int d = 52;
const int ox = 160;
const int oy = 425;
const int r_arc = 90;

const int rgb_x = cx - 2*d;
const int rgb_y = cy - 2*d;
const int btn180_x = cx + 2*d;
const int btn180_y = cy - 2*d;

// Nowe przyciski (Lewy i Prawy dół krzyżaka)
const int btnTheme_x = cx - 2*d;
const int btnTheme_y = cy + 2*d;
const int btnOff_x = cx + 2*d;
const int btnOff_y = cy + 2*d;

// --- MASZYNA STANÓW DOTYKU ---
enum TouchState {
  NONE, BTN_STOP, BTN_RGB, BTN_180, BTN_THEME, BTN_OFF,
  BTN_MODE_L, BTN_MODE_R,
  S_UP_JOG, S_UP_AUTO, S_DN_JOG, S_DN_AUTO,
  M_L_JOG, M_L_AUTO, M_R_JOG, M_R_AUTO
};
TouchState activeTouch = NONE;
bool oneShotTriggered = false;

// --- FUNKCJA DOTYKU ---
bool inRect(int tx, int ty, int bx, int by, int bw, int bh) {
  return (tx >= bx - bw/2 && tx <= bx + bw/2 && ty >= by - bh/2 && ty <= by + bh/2);
}

// --- FUNKCJE SPRZĘTOWE ---
void ustawSerwo(int kat) {
  int duty = map(kat, 0, 270, 80, 532);
  ledcWrite(SERVO_PIN, duty);
}

// --- FUNKCJE RYSUJĄCE GRAFIKĘ ---
void drawArrow(int x, int y, int dir, bool doubleArrow, uint16_t color) {
  int offset = doubleArrow ? 5 : 0;
  for (int i = 0; i < (doubleArrow ? 2 : 1); i++) {
    int cx_a = x, cy_a = y;
    if (dir == 0) cy_a = y - offset + (i * 10);
    else if (dir == 1) cx_a = x + offset - (i * 10);
    else if (dir == 2) cy_a = y + offset - (i * 10);
    else if (dir == 3) cx_a = x - offset + (i * 10);

    if (dir == 0) lcd.fillTriangle(cx_a, cy_a - 5, cx_a - 7, cy_a + 4, cx_a + 7, cy_a + 4, color);
    else if (dir == 1) lcd.fillTriangle(cx_a + 5, cy_a, cx_a - 4, cy_a - 7, cx_a - 4, cy_a + 7, color);
    else if (dir == 2) lcd.fillTriangle(cx_a, cy_a + 5, cx_a - 7, cy_a - 4, cx_a + 7, cy_a - 4, color);
    else if (dir == 3) lcd.fillTriangle(cx_a - 5, cy_a, cx_a + 4, cy_a - 7, cx_a + 4, cy_a + 7, color);
  }
}

void drawDirButton(int x, int y, int dir, bool doubleArrow, uint16_t bgColor) {
  lcd.fillRoundRect(x - 21, y - 21, 42, 42, 5, bgColor);
  drawArrow(x, y, dir, doubleArrow, 0xFFFF);
}

void drawMediumButton(int x, int y, const char* label, uint16_t bgColor) {
  int bw = 76, bh = 46;
  lcd.fillRoundRect(x - bw/2, y - bh/2, bw, bh, 4, bgColor);
  lcd.setTextColor(0xFFFF, bgColor);
  lcd.setTextSize(2);
  lcd.drawCenterString(label, x, y - 7);
}

void drawVerticalText(int x, int y, const char* text, uint16_t color, uint16_t bgColor) {
  lcd.setTextColor(color, bgColor);
  lcd.setTextSize(2);
  int len = strlen(text);
  int startY = y - (len * 16) / 2 + 8;
  for (int i = 0; i < len; i++) {
    char str[2] = {text[i], '\0'};
    lcd.drawCenterString(str, x, startY + i * 16 - 8);
  }
}

void drawModeButtons() {
  uint16_t colorInactive = isDarkMode ? 0x3186 : 0x8410;
  uint16_t colorActive = 0x07E0;

  uint16_t bgL = !modeOhm ? colorActive : colorInactive;
  lcd.fillRoundRect(15, oy - 50, 40, 100, 4, bgL);
  drawVerticalText(35, oy, "BZZ", 0xFFFF, bgL);

  uint16_t bgR = modeOhm ? colorActive : colorInactive;
  lcd.fillRoundRect(265, oy - 50, 40, 100, 4, bgR);
  drawVerticalText(285, oy, "OHM", 0xFFFF, bgR);
}

void drawUI() {
  uint16_t c_bg = isDarkMode ? 0x0000 : 0xFFFF;
  uint16_t c_txt = isDarkMode ? 0xFFFF : 0x0000;
  uint16_t c_line = isDarkMode ? 0x7BEF : 0x8410;

  lcd.fillScreen(c_bg);
  lcd.setTextColor(c_txt, c_bg);
  lcd.setTextSize(2);
  lcd.drawCenterString("PROMIR-BUD STATION", 160, 10);
  lcd.drawLine(0, 35, 320, 35, c_line);

  drawDirButton(cx, cy - d, 0, false, 0x001F);
  drawDirButton(cx, cy + d, 2, false, 0x001F);
  drawDirButton(cx - d, cy, 3, false, 0x001F);
  drawDirButton(cx + d, cy, 1, false, 0x001F);
  drawDirButton(cx, cy - 2*d, 0, true, 0x03E0);
  drawDirButton(cx, cy + 2*d, 2, true, 0x03E0);
  drawDirButton(cx - 2*d, cy, 3, true, 0x03E0);
  drawDirButton(cx + 2*d, cy, 1, true, 0x03E0);

  drawMediumButton(rgb_x, rgb_y, rgbEnabled ? "RGB" : "rgb", rgbEnabled ? 0x07E0 : 0x3186);
  drawMediumButton(btn180_x, btn180_y, "180", 0xD8E4);

  drawMediumButton(btnTheme_x, btnTheme_y, isDarkMode ? "LIGHT" : "DARK", 0x3186);
  drawMediumButton(btnOff_x, btnOff_y, "OFF", 0x3186);

  lcd.fillCircle(cx, cy, 30, 0xF800);
  lcd.drawCircle(cx, cy, 30, 0xFFFF);
  lcd.setTextColor(0xFFFF, 0xF800);
  lcd.setTextSize(2);
  lcd.drawCenterString("STOP", cx, cy - 7);

  lcd.drawArc(ox, oy, r_arc, r_arc-2, 180, 360, c_txt);
  lcd.drawArc(ox, oy, r_arc-10, r_arc-11, 180, 360, c_line);

  drawModeButtons();
}

void updateRGB(int adcVal) {
  if (!rgbEnabled) {
    neopixelWrite(RGB_LED_PIN, 0, 0, 0);
    return;
  }
  if (!modeOhm) {
    if (adcVal < 40) neopixelWrite(RGB_LED_PIN, 0, 255, 0);
    else neopixelWrite(RGB_LED_PIN, 255, 0, 0);
  } else {
    neopixelWrite(RGB_LED_PIN, 0, 0, 255);
  }
}

void updateMultimetr(int adcVal) {
  uint16_t c_bg = isDarkMode ? 0x0000 : 0xFFFF;
  uint16_t c_txt = isDarkMode ? 0xFFFF : 0x0000;

  int r_needle = 75;
  int targetAngle;
  char displayBuf[20];

  lcd.fillRect(ox - 70, oy + 10, 140, 40, c_bg);
  lcd.setTextColor(c_txt, c_bg);
  lcd.setTextSize(2);

  if (!modeOhm) {
    if (adcVal < 40) {
      targetAngle = 345;
      lcd.drawCenterString("SHORT", ox, oy + 20);
    } else {
      targetAngle = 195;
      lcd.drawCenterString("OPEN", ox, oy + 20);
    }
  } else {
    targetAngle = map(adcVal, 0, 4095, 360, 180);
    if (targetAngle > 360) targetAngle = 360;
    if (targetAngle < 180) targetAngle = 180;

    if (adcVal >= 4080) {
      lcd.drawCenterString("O.L", ox, oy + 20);
    } else if (adcVal < 10) {
      lcd.drawCenterString("0 Ohm", ox, oy + 20);
    } else {
      float r = 45000.0 * adcVal / (4095.0 - adcVal);
      if (r < 1000) {
        sprintf(displayBuf, "%d Ohm", (int)r);
      } else if (r < 1000000) {
        sprintf(displayBuf, "%.1f kOhm", r / 1000.0);
      } else {
        sprintf(displayBuf, "O.L");
      }
      lcd.drawCenterString(displayBuf, ox, oy + 20);
    }
  }

  float rad = lastAngle * PI / 180.0;
  lcd.drawLine(ox, oy, ox + cos(rad) * r_needle, oy + sin(rad) * r_needle, c_bg);

  rad = targetAngle * PI / 180.0;
  lcd.drawLine(ox, oy, ox + cos(rad) * r_needle, oy + sin(rad) * r_needle, c_txt);
  lcd.fillCircle(ox, oy, 6, 0x7BEF);

  updateRGB(adcVal);
  lastAngle = targetAngle;
}

void setup() {
  Serial.begin(115200);

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(DIR_PIN, HIGH);
  digitalWrite(EN_PIN, HIGH);

  pinMode(LIMIT_1, INPUT_PULLUP);
  pinMode(LIMIT_2, INPUT_PULLUP);
  pinMode(TOUCH_IRQ, INPUT_PULLUP);
  pinMode(CONTINUITY_PIN, INPUT_PULLUP);

  neopixelWrite(RGB_LED_PIN, 0, 0, 0);

  ledcAttach(SERVO_PIN, 50, 12);
  ustawSerwo(135);

  lcd.init();
  lcd.setRotation(2);
  drawUI();

  lastAdc = analogRead(CONTINUITY_PIN);
  pinMode(CONTINUITY_PIN, INPUT_PULLUP);
  updateMultimetr(lastAdc);
}

void loop() {
  static unsigned long lastOmomierzUpdate = 0;
  if (millis() - lastOmomierzUpdate > 50) {
    int adcSum = 0;
    for (int i = 0; i < 5; i++) {
      adcSum += analogRead(CONTINUITY_PIN);
      delayMicroseconds(50);
    }
    int averageAdc = adcSum / 5;
    pinMode(CONTINUITY_PIN, INPUT_PULLUP);

    if (abs(averageAdc - lastAdc) > 10) {
      lastAdc = averageAdc;
      if (!screenAsleep) {
        updateMultimetr(averageAdc);
      }
    }
    lastOmomierzUpdate = millis();
  }

  if (motorRunning && !isReversing) {
    if (digitalRead(LIMIT_1) == LOW || digitalRead(LIMIT_2) == LOW) {
      motorRunning = false;
      isJogging = false;
      isReversing = true;
      stepsRemaining = STEPS_PER_REV;
      digitalWrite(DIR_PIN, !digitalRead(DIR_PIN));
    }
  }

  if (millis() - lastServoTime >= servoSpeedMs) {
    lastServoTime = millis();
    if (currentServoAngle < targetServoAngle) {
      currentServoAngle++;
      ustawSerwo(currentServoAngle);
    } else if (currentServoAngle > targetServoAngle) {
      currentServoAngle--;
      ustawSerwo(currentServoAngle);
    }
  }

  if (digitalRead(TOUCH_IRQ) == LOW) {
    int32_t touchX, touchY;
    if (lcd.getTouch(&touchX, &touchY)) {
      touchY = 480 - touchY;

      if (screenAsleep) {
        screenAsleep = false;
        drawUI();
        updateMultimetr(lastAdc);
        activeTouch = NONE;
        delay(300);
      } else {
        if (activeTouch == NONE) {
          if (inRect(touchX, touchY, cx, cy, 60, 60)) activeTouch = BTN_STOP;
          else if (inRect(touchX, touchY, rgb_x, rgb_y, 76, 50)) activeTouch = BTN_RGB;
          else if (inRect(touchX, touchY, btn180_x, btn180_y, 76, 50)) activeTouch = BTN_180;
          else if (inRect(touchX, touchY, btnTheme_x, btnTheme_y, 76, 50)) activeTouch = BTN_THEME;
          else if (inRect(touchX, touchY, btnOff_x, btnOff_y, 76, 50)) activeTouch = BTN_OFF;
          else if (inRect(touchX, touchY, 35, oy, 60, 110)) activeTouch = BTN_MODE_L;
          else if (inRect(touchX, touchY, 285, oy, 60, 110)) activeTouch = BTN_MODE_R;
          else if (!isReversing) {
            if (inRect(touchX, touchY, cx, cy - d, 48, 48)) activeTouch = S_UP_JOG;
            else if (inRect(touchX, touchY, cx, cy - 2*d, 48, 48)) activeTouch = S_UP_AUTO;
            else if (inRect(touchX, touchY, cx, cy + d, 48, 48)) activeTouch = S_DN_JOG;
            else if (inRect(touchX, touchY, cx, cy + 2*d, 48, 48)) activeTouch = S_DN_AUTO;
            else if (inRect(touchX, touchY, cx - d, cy, 48, 48)) activeTouch = M_L_JOG;
            else if (inRect(touchX, touchY, cx - 2*d, cy, 48, 48)) activeTouch = M_L_AUTO;
            else if (inRect(touchX, touchY, cx + d, cy, 48, 48)) activeTouch = M_R_JOG;
            else if (inRect(touchX, touchY, cx + 2*d, cy, 48, 48)) activeTouch = M_R_AUTO;
          }
        }

        switch (activeTouch) {
          case BTN_STOP:
            motorRunning = false; isReversing = false; isJogging = false;
            targetServoAngle = currentServoAngle;
            break;

          case BTN_RGB:
            if (!oneShotTriggered) {
              rgbEnabled = !rgbEnabled;
              drawMediumButton(rgb_x, rgb_y, rgbEnabled ? "RGB" : "rgb", rgbEnabled ? 0x07E0 : 0x3186);
              updateRGB(lastAdc);
              oneShotTriggered = true;
            }
            break;

          case BTN_180:
            if (!oneShotTriggered) {
              if (currentServoAngle <= 135) {
                targetServoAngle = currentServoAngle + 180;
                if (targetServoAngle > 270) targetServoAngle = 270;
              } else {
                targetServoAngle = currentServoAngle - 180;
                if (targetServoAngle < 0) targetServoAngle = 0;
              }
              isJogging = false;
              oneShotTriggered = true;
            }
            break;

          case BTN_THEME:
            if (!oneShotTriggered) {
              isDarkMode = !isDarkMode;
              drawUI();
              updateMultimetr(lastAdc);
              oneShotTriggered = true;
            }
            break;

          case BTN_OFF:
            if (!oneShotTriggered) {
              screenAsleep = true;
              lcd.fillScreen(0x0000);
              oneShotTriggered = true;
            }
            break;

          case BTN_MODE_L:
            if (!oneShotTriggered && modeOhm) {
              modeOhm = false; drawModeButtons(); updateMultimetr(lastAdc); oneShotTriggered = true;
            }
            break;

          case BTN_MODE_R:
            if (!oneShotTriggered && !modeOhm) {
              modeOhm = true; drawModeButtons(); updateMultimetr(lastAdc); oneShotTriggered = true;
            }
            break;

          case S_UP_JOG:   targetServoAngle = 0; isJogging = true; break;
          case S_UP_AUTO:  targetServoAngle = 0; isJogging = false; break;
          case S_DN_JOG:   targetServoAngle = 270; isJogging = true; break;
          case S_DN_AUTO:  targetServoAngle = 270; isJogging = false; break;
          case M_L_JOG:    digitalWrite(DIR_PIN, LOW); motorRunning = true; isJogging = true; break;
          case M_L_AUTO:   digitalWrite(DIR_PIN, LOW); motorRunning = true; isJogging = false; break;
          case M_R_JOG:    digitalWrite(DIR_PIN, HIGH); motorRunning = true; isJogging = true; break;
          case M_R_AUTO:   digitalWrite(DIR_PIN, HIGH); motorRunning = true; isJogging = false; break;

          default: break;
        }
      }
    }
  } else {
    if (activeTouch != NONE) {
      if (isJogging) {
        motorRunning = false;
        targetServoAngle = currentServoAngle;
        isJogging = false;
      }
      activeTouch = NONE;
      oneShotTriggered = false;
    }
  }

  if (motorRunning || isReversing) {
    digitalWrite(EN_PIN, LOW);
    unsigned long currentMicros = micros();
    if (currentMicros - lastStepTime >= stepDelayMicros) {
      lastStepTime = currentMicros;
      digitalWrite(STEP_PIN, HIGH);
      delayMicroseconds(15);
      digitalWrite(STEP_PIN, LOW);

      if (isReversing) {
        stepsRemaining--;
        if (stepsRemaining <= 0) isReversing = false;
      }
    }
  } else {
    digitalWrite(EN_PIN, HIGH);
  }
}
