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
const int snoozeButtonPin = 18;
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
const unsigned long alarmBeepLength = 180;
const unsigned long alarmPauseLength = 120;

// ===== Elegoo Motion Trigger =====
const int motionTriggerPin = 19;
const unsigned long motionTriggerPulseLength = 500;

// ===== Relay =====
const int relayPin = 17;
const int relayOnSignal = LOW;
const int relayOffSignal = HIGH;
const unsigned long relayStartDelay = 5000;
const unsigned long relayOnDuration = 12000;

// ===== Servo =====
const int servoPin = 16;
const int servoChannel = 1;
const int servoResolution = 16;
const int servoFrequency = 50;
const int servoMinPulse = 500;
const int servoMaxPulse = 2500;
const int servoPeriod = 20000;
const int servoStartAngle = 90;
const int servoStartupWiggle = 6;
const int servoAlarmSwing = 90;
const unsigned long alarmServoMoveDelay = 200;

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
  MENU_SET_ALARM_TIME,
  MENU_DEV_TRIGGER_MOTION
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
bool alarmRinging = false;
unsigned long lastDisplayUpdate = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long lastNtpAttempt = 0;
unsigned long lastSnoozeButtonChange = 0;
unsigned long lastSwitchChange = 0;
unsigned long switchPressedAt = 0;
unsigned long lastJoystickAction = 0;
unsigned long alarmStartedAt = 0;
unsigned long lastAlarmBeepChange = 0;
unsigned long lastAlarmServoMove = 0;
unsigned long relayStartedAt = 0;
int lastAlarmTriggerDay = -1;
bool alarmBeepOn = false;
bool alarmServoAwayFromStart = false;
bool relayOn = false;
bool relayComplete = false;
bool motionStarted = false;
int snoozeButtonState = HIGH;
int lastSnoozeButtonState = HIGH;
int stableSnoozeButtonState = HIGH;
int switchState = HIGH;
int lastSwitchState = HIGH;
int stableSwitchState = HIGH;
bool displayDirty = true;

void beginAlarmSequence();

const char* wifiStatusText(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "IDLE";
    case WL_NO_SSID_AVAIL:
      return "NO_SSID";
    case WL_SCAN_COMPLETED:
      return "SCAN_DONE";
    case WL_CONNECTED:
      return "CONNECTED";
    case WL_CONNECT_FAILED:
      return "FAIL";
    case WL_CONNECTION_LOST:
      return "LOST";
    case WL_DISCONNECTED:
      return "DISCONNECTED";
    default:
      return "UNKNOWN";
  }
}

void stopBuzzer() {
  digitalWrite(buzzerPin, LOW);
}

void pulseMotionTrigger() {
  digitalWrite(motionTriggerPin, HIGH);
  delay(motionTriggerPulseLength);
  digitalWrite(motionTriggerPin, LOW);
}

void setRelay(bool enabled) {
  relayOn = enabled;
  digitalWrite(relayPin, enabled ? relayOnSignal : relayOffSignal);
}

void writeServoAngle(int angle) {
  angle = constrain(angle, 0, 180);
  int pulseWidth = map(angle, 0, 180, servoMinPulse, servoMaxPulse);
  uint32_t duty = ((uint32_t)pulseWidth * ((1 << servoResolution) - 1)) / servoPeriod;
  ledcWrite(servoChannel, duty);
}

void moveStartupServo() {
  ledcSetup(servoChannel, servoFrequency, servoResolution);
  ledcAttachPin(servoPin, servoChannel);

  writeServoAngle(servoStartAngle);
  delay(300);
  writeServoAngle(servoStartAngle + servoStartupWiggle);
  delay(250);
  writeServoAngle(servoStartAngle - servoStartupWiggle);
  delay(250);
  writeServoAngle(servoStartAngle);
  delay(300);
}

void stopAlarm() {
  if (motionStarted) {
    pulseMotionTrigger();
  }

  alarmRinging = false;
  alarmBeepOn = false;
  alarmServoAwayFromStart = false;
  motionStarted = false;
  relayComplete = false;
  setRelay(false);
  stopBuzzer();
  writeServoAngle(servoStartAngle);
  displayDirty = true;
}

void playStartupAlarm() {
  const unsigned long beepLength = 100;
  const unsigned long pauseLength = 120;

  pinMode(buzzerPin, OUTPUT);

  for (int beep = 0; beep < 2; beep++) {
    digitalWrite(buzzerPin, HIGH);
    delay(beepLength);
    digitalWrite(buzzerPin, LOW);
    delay(pauseLength);
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
      Serial.println();
      Serial.print("WiFi connect timeout: ");
      Serial.println(wifiStatusText(WiFi.status()));
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
      Serial.print("Retrying WiFi: ");
      Serial.println(wifiStatusText(WiFi.status()));
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
  wl_status_t wifiStatus = WiFi.status();
  if (wifiStatus != WL_CONNECTED) {
    timeSynced = false;
    wifiConnected = false;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi ");
    lcd.print(wifiStatusText(wifiStatus));
    lcd.setCursor(0, 1);
    lcd.print("Retrying...");
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

  strftime(line1, sizeof(line1), "%I:%M:%S %p", &timeInfo);
  strftime(line2, sizeof(line2), "%m/%d/%Y %a", &timeInfo);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void displayAlarmRinging() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ALARM!");
  lcd.setCursor(0, 1);
  lcd.print("D18 to stop");
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
  alarmEnabled = true;
  lastAlarmTriggerDay = -1;
  displayDirty = true;
}

void displayAlarmMenu() {
  char line1[17];
  const char* selectedText = ">Toggle Alarm";

  if (menuSelection == MENU_SET_ALARM_TIME) {
    selectedText = ">Set Time";
  } else if (menuSelection == MENU_DEV_TRIGGER_MOTION) {
    selectedText = ">Dev Move Test";
  }

  snprintf(
    line1,
    sizeof(line1),
    "Alarm %s %02d:%02d%s",
    alarmEnabled ? "ON " : "OFF",
    to12Hour(alarmHour),
    alarmMinute,
    alarmHour >= 12 ? "P" : "A"
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
  if (menuSelection == MENU_TOGGLE_ALARM) {
    menuSelection = MENU_SET_ALARM_TIME;
  } else if (menuSelection == MENU_SET_ALARM_TIME) {
    menuSelection = MENU_DEV_TRIGGER_MOTION;
  } else {
    menuSelection = MENU_TOGGLE_ALARM;
  }
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
    } else if (menuSelection == MENU_SET_ALARM_TIME) {
      loadDraftAlarmTime();
      currentScreen = SCREEN_SET_ALARM;
      displayDirty = true;
    } else if (menuSelection == MENU_DEV_TRIGGER_MOTION) {
      beginAlarmSequence();
    }
  } else if (currentScreen == SCREEN_SET_ALARM && setAlarmSelection == SET_FIELD_CONFIRM) {
    saveDraftAlarmTime();
    currentScreen = SCREEN_ALARM_MENU;
    displayDirty = true;
  }
}

void readSnoozeButton() {
  snoozeButtonState = digitalRead(snoozeButtonPin);

  if (snoozeButtonState != lastSnoozeButtonState) {
    lastSnoozeButtonChange = millis();
  }

  if (millis() - lastSnoozeButtonChange > debounceDelay) {
    if (snoozeButtonState != stableSnoozeButtonState) {
      stableSnoozeButtonState = snoozeButtonState;
      if (stableSnoozeButtonState == LOW && alarmRinging) {
        stopAlarm();
      }
    }
  }

  lastSnoozeButtonState = snoozeButtonState;
}

void beginAlarmSequence() {
  alarmRinging = true;
  alarmStartedAt = millis();
  lastAlarmBeepChange = millis();
  lastAlarmServoMove = millis();
  relayStartedAt = 0;
  alarmBeepOn = true;
  alarmServoAwayFromStart = true;
  relayComplete = false;
  motionStarted = false;
  setRelay(false);
  digitalWrite(buzzerPin, HIGH);
  writeServoAngle(servoStartAngle + servoAlarmSwing);
  currentScreen = SCREEN_CLOCK;
  displayDirty = true;
}

void startAlarm(const tm& timeInfo) {
  lastAlarmTriggerDay = timeInfo.tm_yday;
  beginAlarmSequence();
}

void updateAlarmSound() {
  if (!alarmRinging) {
    return;
  }

  unsigned long interval = alarmBeepOn ? alarmBeepLength : alarmPauseLength;
  unsigned long now = millis();
  if (now - lastAlarmBeepChange >= interval) {
    alarmBeepOn = !alarmBeepOn;
    digitalWrite(buzzerPin, alarmBeepOn ? HIGH : LOW);
    lastAlarmBeepChange = now;
  }
}

void updateAlarmServo() {
  if (!alarmRinging || relayOn || relayComplete || millis() - lastAlarmServoMove < alarmServoMoveDelay) {
    return;
  }

  alarmServoAwayFromStart = !alarmServoAwayFromStart;
  writeServoAngle(alarmServoAwayFromStart ? servoStartAngle + servoAlarmSwing : servoStartAngle);
  lastAlarmServoMove = millis();
}

void updateAlarmRelayAndMotion() {
  if (!alarmRinging || motionStarted) {
    return;
  }

  unsigned long now = millis();
  if (!relayOn && !relayComplete && now - alarmStartedAt >= relayStartDelay) {
    writeServoAngle(servoStartAngle);
    relayStartedAt = now;
    setRelay(true);
    return;
  }

  if (relayOn && now - relayStartedAt >= relayOnDuration) {
    setRelay(false);
    relayComplete = true;
    pulseMotionTrigger();
    motionStarted = true;
  }
}

void checkAlarm() {
  if (!alarmEnabled || alarmRinging || !timeSynced) {
    return;
  }

  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 50)) {
    timeSynced = false;
    return;
  }

  if (
    timeInfo.tm_hour == alarmHour &&
    timeInfo.tm_min == alarmMinute &&
    timeInfo.tm_yday != lastAlarmTriggerDay
  ) {
    startAlarm(timeInfo);
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

  int yDirection = -readJoystickDirection(joystickYPin, joystickYCenter);
  int xDirection = readJoystickDirection(joystickXPin, joystickXCenter);

  if (currentScreen == SCREEN_ALARM_MENU) {
    if (xDirection != 0) {
      selectNextMenuOption();
      lastJoystickAction = millis();
    }
  } else if (currentScreen == SCREEN_SET_ALARM) {
    if (yDirection != 0) {
      moveSetAlarmSelection(yDirection);
      lastJoystickAction = millis();
    } else if (xDirection != 0) {
      adjustDraftAlarmValue(-xDirection);
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

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, relayOffSignal);
  pinMode(motionTriggerPin, OUTPUT);
  digitalWrite(motionTriggerPin, LOW);
  pinMode(snoozeButtonPin, INPUT_PULLUP);
  pinMode(joystickSwitchPin, INPUT_PULLUP);
  pinMode(joystickXPin, INPUT);
  pinMode(joystickYPin, INPUT);
  analogReadResolution(12);
  joystickXCenter = calibrateJoystickCenter(joystickXPin);
  joystickYCenter = calibrateJoystickCenter(joystickYPin);
  moveStartupServo();
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
  readSnoozeButton();
  readJoystick();
  retryTimeSyncIfNeeded();
  checkAlarm();
  updateAlarmSound();
  updateAlarmServo();
  updateAlarmRelayAndMotion();

  unsigned long now = millis();
  if (alarmRinging) {
    if (displayDirty) {
      displayAlarmRinging();
      displayDirty = false;
    }
  } else if (currentScreen == SCREEN_ALARM_MENU) {
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
