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
const unsigned long wifiRetryDelay = 15000;
const unsigned long ntpRetryDelay = 10000;

// ===== Joystick =====
const int joystickSwitchPin = 5;
const int joystickYPin = 33;
const int joystickXPin = 32;
const unsigned long debounceDelay = 50;
const unsigned long longPressDelay = 1200;
const unsigned long joystickActionDelay = 300;
const int joystickDeadZone = 900;
int joystickXCenter = 2048;
int joystickYCenter = 2048;

// ===== Buzzer =====
const int buzzerPin = 23;
const unsigned long startupAlarmDuration = 3000;

bool alarmEnabled = false;
int alarmHour = 7;
int alarmMinute = 0;

enum Screen {
  SCREEN_CLOCK,
  SCREEN_ALARM_MENU,
  SCREEN_SET_ALARM
};

enum MenuOption {
  MENU_TOGGLE_ALARM,
  MENU_SET_ALARM_TIME
};

enum SetAlarmField {
  SET_FIELD_HOUR,
  SET_FIELD_MINUTE,
  SET_FIELD_AMPM,
  SET_FIELD_CONFIRM
};

Screen currentScreen = SCREEN_CLOCK;
MenuOption menuSelection = MENU_TOGGLE_ALARM;
SetAlarmField setAlarmSelection = SET_FIELD_HOUR;
int draftAlarmHour = 8;
int draftAlarmMinute = 0;
bool draftAlarmPm = false;
bool wifiConnected = false;
bool timeSynced = false;
unsigned long lastDisplayUpdate = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long lastNtpAttempt = 0;
unsigned long lastSwitchChange = 0;
unsigned long switchPressedAt = 0;
unsigned long lastJoystickAction = 0;
int switchState = HIGH;
int lastSwitchState = HIGH;
int stableSwitchState = HIGH;
bool displayDirty = true;

void stopBuzzer() {
  digitalWrite(buzzerPin, LOW);
}

void playStartupAlarm() {
  const unsigned long beepLength = 120;
  const unsigned long pauseLength = 90;
  const unsigned long groupPauseLength = 320;

  pinMode(buzzerPin, OUTPUT);

  unsigned long startedAt = millis();
  while (millis() - startedAt < startupAlarmDuration) {
    for (int beep = 0; beep < 4 && millis() - startedAt < startupAlarmDuration; beep++) {
      digitalWrite(buzzerPin, HIGH);
      delay(beepLength);
      digitalWrite(buzzerPin, LOW);
      delay(pauseLength);
    }

    delay(groupPauseLength);
  }

  stopBuzzer();
}

bool setupWiFi() {
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (millis() - start > 20000) {
      Serial.println("\nWiFi connect timeout");
      return false;
    }
  }

  Serial.println();
  Serial.print("Connected, IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

bool setupTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("NTP configured");

  struct tm timeInfo;
  if (getLocalTime(&timeInfo, 8000)) {
    Serial.println("NTP synced");
    return true;
  }

  Serial.println("NTP sync timeout");
  return false;
}

void retryTimeSyncIfNeeded() {
  if (timeSynced) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    if (millis() - lastWiFiAttempt >= wifiRetryDelay) {
      lastWiFiAttempt = millis();
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      displayDirty = true;
    }
    return;
  }

  wifiConnected = true;
  if (millis() - lastNtpAttempt < ntpRetryDelay) {
    return;
  }

  lastNtpAttempt = millis();
  timeSynced = setupTime();
  displayDirty = true;
}

void displayDateTime() {
  struct tm timeInfo;
  if (WiFi.status() != WL_CONNECTED) {
    timeSynced = false;
    wifiConnected = false;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi not ready");
    lcd.setCursor(0, 1);
    lcd.print("Check network");
    return;
  }

  wifiConnected = true;
  if (!getLocalTime(&timeInfo, 250)) {
    timeSynced = false;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("NTP syncing...");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    return;
  }

  timeSynced = true;
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

int to12Hour(int hour24) {
  int hour12 = hour24 % 12;
  return hour12 == 0 ? 12 : hour12;
}

void loadDraftAlarmTime() {
  draftAlarmHour = to12Hour(alarmHour);
  draftAlarmMinute = alarmMinute;
  draftAlarmPm = alarmHour >= 12;
  setAlarmSelection = SET_FIELD_HOUR;
}

void saveDraftAlarmTime() {
  int hour24 = draftAlarmHour % 12;
  if (draftAlarmPm) {
    hour24 += 12;
  }

  alarmHour = hour24;
  alarmMinute = draftAlarmMinute;
  displayDirty = true;
}

void displayAlarmMenu() {
  char line1[17];
  const char* selectedText = menuSelection == MENU_TOGGLE_ALARM ? ">Toggle Alarm" : ">Set Time";

  snprintf(
    line1,
    sizeof(line1),
    "Alarm %s %02d:%02d",
    alarmEnabled ? "ON " : "OFF",
    to12Hour(alarmHour),
    alarmMinute
  );

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(selectedText);
}

void displaySetAlarmTime() {
  char line1[17];
  char line2[17] = "                ";
  int caretPosition = 0;

  snprintf(
    line1,
    sizeof(line1),
    "%02d:%02d %s    OK",
    draftAlarmHour,
    draftAlarmMinute,
    draftAlarmPm ? "PM" : "AM"
  );

  if (setAlarmSelection == SET_FIELD_HOUR) {
    caretPosition = 0;
  } else if (setAlarmSelection == SET_FIELD_MINUTE) {
    caretPosition = 3;
  } else if (setAlarmSelection == SET_FIELD_AMPM) {
    caretPosition = 6;
  } else {
    caretPosition = 12;
  }

  line2[caretPosition] = '^';
  line2[16] = '\0';

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void toggleAlarm() {
  alarmEnabled = !alarmEnabled;
  displayDirty = true;
  if (currentScreen == SCREEN_ALARM_MENU) {
    displayAlarmMenu();
    displayDirty = false;
  }
}

void selectNextMenuOption() {
  menuSelection = menuSelection == MENU_TOGGLE_ALARM ? MENU_SET_ALARM_TIME : MENU_TOGGLE_ALARM;
  displayDirty = true;
}

void moveSetAlarmSelection(int direction) {
  int nextSelection = (int)setAlarmSelection + direction;
  if (nextSelection < 0) {
    nextSelection = SET_FIELD_CONFIRM;
  } else if (nextSelection > SET_FIELD_CONFIRM) {
    nextSelection = SET_FIELD_HOUR;
  }
  setAlarmSelection = (SetAlarmField)nextSelection;
  displayDirty = true;
}

void adjustDraftAlarmValue(int direction) {
  if (setAlarmSelection == SET_FIELD_HOUR) {
    draftAlarmHour += direction;
    if (draftAlarmHour > 12) {
      draftAlarmHour = 1;
    } else if (draftAlarmHour < 1) {
      draftAlarmHour = 12;
    }
  } else if (setAlarmSelection == SET_FIELD_MINUTE) {
    draftAlarmMinute += direction;
    if (draftAlarmMinute > 59) {
      draftAlarmMinute = 0;
    } else if (draftAlarmMinute < 0) {
      draftAlarmMinute = 59;
    }
  } else if (setAlarmSelection == SET_FIELD_AMPM) {
    draftAlarmPm = !draftAlarmPm;
  }

  displayDirty = true;
}

void enterExitAlarmMenu() {
  if (currentScreen == SCREEN_CLOCK) {
    currentScreen = SCREEN_ALARM_MENU;
  } else {
    currentScreen = SCREEN_CLOCK;
  }
  displayDirty = true;
}

void handleShortPressEvent() {
  if (currentScreen == SCREEN_ALARM_MENU) {
    if (menuSelection == MENU_TOGGLE_ALARM) {
      toggleAlarm();
    } else {
      loadDraftAlarmTime();
      currentScreen = SCREEN_SET_ALARM;
      displayDirty = true;
    }
  } else if (currentScreen == SCREEN_SET_ALARM && setAlarmSelection == SET_FIELD_CONFIRM) {
    saveDraftAlarmTime();
    currentScreen = SCREEN_ALARM_MENU;
    displayDirty = true;
  }
}

void readJoystickSwitch() {
  switchState = digitalRead(joystickSwitchPin);

  if (switchState != lastSwitchState) {
    lastSwitchChange = millis();
  }

  if (millis() - lastSwitchChange > debounceDelay) {
    if (switchState != stableSwitchState) {
      stableSwitchState = switchState;
      if (stableSwitchState == LOW) {
        switchPressedAt = millis();
      } else {
        unsigned long pressDuration = millis() - switchPressedAt;
        if (pressDuration >= longPressDelay) {
          enterExitAlarmMenu();
        } else {
          handleShortPressEvent();
        }
      }
    }
  }

  lastSwitchState = switchState;
}

int readJoystickDirection(int pin, int center) {
  int value = analogRead(pin);
  if (value < center - joystickDeadZone) {
    return -1;
  }
  if (value > center + joystickDeadZone) {
    return 1;
  }
  return 0;
}

void readJoystickAxes() {
  if (currentScreen == SCREEN_CLOCK || millis() - lastJoystickAction < joystickActionDelay) {
    return;
  }

  int yDirection = readJoystickDirection(joystickYPin, joystickYCenter);
  int xDirection = readJoystickDirection(joystickXPin, joystickXCenter);

  if (currentScreen == SCREEN_ALARM_MENU) {
    if (yDirection != 0) {
      selectNextMenuOption();
      lastJoystickAction = millis();
    }
  } else if (currentScreen == SCREEN_SET_ALARM) {
    if (xDirection != 0) {
      moveSetAlarmSelection(xDirection);
      lastJoystickAction = millis();
    } else if (yDirection != 0) {
      adjustDraftAlarmValue(-yDirection);
      lastJoystickAction = millis();
    }
  }
}

void readJoystick() {
  readJoystickSwitch();
  readJoystickAxes();
}

int calibrateJoystickCenter(int pin) {
  long total = 0;
  const int samples = 30;
  for (int sample = 0; sample < samples; sample++) {
    total += analogRead(pin);
    delay(5);
  }
  return total / samples;
}

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(joystickSwitchPin, INPUT_PULLUP);
  pinMode(joystickXPin, INPUT);
  pinMode(joystickYPin, INPUT);
  analogReadResolution(12);
  joystickXCenter = calibrateJoystickCenter(joystickXPin);
  joystickYCenter = calibrateJoystickCenter(joystickYPin);
  playStartupAlarm();

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WHACKACLOCK");
  lcd.setCursor(0, 1);
  lcd.print("Date & Time UI");

  lastWiFiAttempt = millis();
  wifiConnected = setupWiFi();
  if (wifiConnected) {
    lastNtpAttempt = millis();
    timeSynced = setupTime();
  }
  delay(1500);
  lcd.clear();
  displayDirty = true;
  lastDisplayUpdate = millis();
}

void loop() {
  readJoystick();
  retryTimeSyncIfNeeded();

  unsigned long now = millis();
  if (currentScreen == SCREEN_ALARM_MENU) {
    if (displayDirty) {
      displayAlarmMenu();
      displayDirty = false;
    }
  } else if (currentScreen == SCREEN_SET_ALARM) {
    if (displayDirty) {
      displaySetAlarmTime();
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
