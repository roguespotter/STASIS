#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "time.h"

// ===== LCD =====
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===== WiFi =====
const char* ssid = "AH - Guest";
const char* password = "NoTimidSouls";

// ===== NTP =====
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -6 * 3600;  // Austin, TX (CST)
const int daylightOffset_sec = 3600;

// ===== Button =====
const int buttonPin = 18;
const unsigned long debounceDelay = 50;
const unsigned long longPressDelay = 1200;
const unsigned long doublePressWindow = 400;

bool alarmEnabled = false;
int alarmHour = 7;
int alarmMinute = 0;

bool alarmMenuActive = false;
unsigned long lastDisplayUpdate = 0;
unsigned long lastButtonChange = 0;
unsigned long buttonPressedAt = 0;
unsigned long lastShortPressAt = 0;
int buttonState = HIGH;
int lastButtonState = HIGH;
int stableButtonState = HIGH;
bool displayDirty = true;

void setupWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (millis() - start > 20000) {
      Serial.println("\nWiFi connect timeout");
      return;
    }
  }

  Serial.println();
  Serial.print("Connected, IP: ");
  Serial.println(WiFi.localIP());
}

void setupTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("NTP configured");
}

void displayDateTime() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Waiting for NTP");
    lcd.setCursor(0, 1);
    lcd.print("sync...");
    return;
  }

  char line1[17];
  char line2[17];

  strftime(line1, sizeof(line1), "%H:%M:%S   %Z", &timeInfo);
  strftime(line2, sizeof(line2), "%m/%d/%Y %a", &timeInfo);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void displayAlarmMenu() {
  char line1[17];
  char line2[17];

  snprintf(line1, sizeof(line1), "Alarm: %s", alarmEnabled ? "ON " : "OFF");
  snprintf(line2, sizeof(line2), "%02d:%02d  +10m tog", alarmHour, alarmMinute);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void toggleAlarm() {
  alarmEnabled = !alarmEnabled;
  displayDirty = true;
  if (alarmMenuActive) {
    displayAlarmMenu();
    displayDirty = false;
  }
}

void incrementAlarmTime() {
  alarmMinute += 10;
  if (alarmMinute >= 60) {
    alarmMinute -= 60;
    alarmHour = (alarmHour + 1) % 24;
  }
  displayDirty = true;
  if (alarmMenuActive) {
    displayAlarmMenu();
    displayDirty = false;
  }
}

void handleShortPressEvent() {
  if (!alarmMenuActive) {
    return;
  }

  unsigned long now = millis();
  if (now - lastShortPressAt <= doublePressWindow) {
    incrementAlarmTime();
  } else {
    toggleAlarm();
  }

  lastShortPressAt = now;
}

void enterExitAlarmMenu() {
  alarmMenuActive = !alarmMenuActive;
  displayDirty = true;
}

void readButton() {
  buttonState = digitalRead(buttonPin);

  if (buttonState != lastButtonState) {
    lastButtonChange = millis();
  }

  if (millis() - lastButtonChange > debounceDelay) {
    if (buttonState != stableButtonState) {
      stableButtonState = buttonState;
      if (stableButtonState == LOW) {
        buttonPressedAt = millis();
      } else {
        unsigned long pressDuration = millis() - buttonPressedAt;
        if (pressDuration >= longPressDelay) {
          enterExitAlarmMenu();
        } else {
          handleShortPressEvent();
        }
      }
    }
  }

  lastButtonState = buttonState;
}

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(buttonPin, INPUT_PULLUP);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WHACKACLOCK");
  lcd.setCursor(0, 1);
  lcd.print("Date & Time UI");

  setupWiFi();
  setupTime();
  delay(1500);
  lcd.clear();
  displayDirty = true;
  lastDisplayUpdate = millis();
}

void loop() {
  readButton();

  unsigned long now = millis();
  if (alarmMenuActive) {
    if (displayDirty) {
      displayAlarmMenu();
      displayDirty = false;
    }
  } else {
    if (now - lastDisplayUpdate >= 1000 || displayDirty) {
      displayDateTime();
      lastDisplayUpdate = now;
      displayDirty = false;
    }
  }

  delay(50);
}

