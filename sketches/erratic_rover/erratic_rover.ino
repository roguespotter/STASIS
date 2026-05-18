/*
  ELEGOO Smart Robot Car Kit V4.0 / UNO R3 erratic rover.

  Motor pins follow the ELEGOO files included with this robot:
    PWMA -> D5
    PWMB -> D6
    AIN  -> D7
    BIN  -> D8
    STBY -> D3
    ESP alarm trigger -> D9

  This sketch assumes only the motors/motor board are connected. It does not
  read ultrasonic, line tracking, IR, Bluetooth, servo, camera, or LEDs.

  Serial commands:
    s = stop/disarm motors
    r = resume random driving
*/

const uint8_t PIN_RIGHT_SPEED = 5;
const uint8_t PIN_LEFT_SPEED = 6;
const uint8_t PIN_RIGHT_DIRECTION = 7;
const uint8_t PIN_LEFT_DIRECTION = 8;
const uint8_t PIN_MOTOR_STBY = 3;
const uint8_t PIN_ALARM_TRIGGER = 9;

const uint8_t RIGHT_MOTOR = 0;
const uint8_t LEFT_MOTOR = 1;

bool armed = false;
bool lastTriggerState = LOW;
unsigned long lastTriggerChangeMs = 0;
const unsigned int TRIGGER_DEBOUNCE_MS = 180;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LEFT_SPEED, OUTPUT);
  pinMode(PIN_RIGHT_SPEED, OUTPUT);
  pinMode(PIN_LEFT_DIRECTION, OUTPUT);
  pinMode(PIN_RIGHT_DIRECTION, OUTPUT);
  pinMode(PIN_MOTOR_STBY, OUTPUT);
  pinMode(PIN_ALARM_TRIGGER, INPUT);
  digitalWrite(PIN_MOTOR_STBY, HIGH);
  stopMotors();

  randomSeed(analogRead(A0) ^ analogRead(A1) ^ micros());

  lastTriggerState = digitalRead(PIN_ALARM_TRIGGER);

  Serial.println(F("Runaway alarm rover ready."));
  Serial.println(F("Pulse D9 HIGH to start; pulse it HIGH again to stop."));
  Serial.println(F("Serial: send 's' to stop, 'r' to resume."));
}

void loop() {
  handleSerial();

  if (!armed) {
    stopMotors();
    delayWithSerial(50);
    return;
  }

  runRandomMove();
}

void runRandomMove() {
  bool startForward = random(0, 100) < 75;
  uint8_t cruiseSpeed = random(235, 256);
  uint8_t turnSpeed = random(225, 256);

  Serial.println(startForward ? F("5s forward-ish run") : F("5s backward-ish run"));

  drive(startForward, cruiseSpeed, startForward, cruiseSpeed);
  delayWithSerial(random(2400, 3600));

  if (random(0, 100) < 35) {
    bool veerLeft = random(0, 2) == 0;
    Serial.println(veerLeft ? F("wide left drift") : F("wide right drift"));
    if (veerLeft) {
      drive(startForward, 255, startForward, random(130, 190));
    } else {
      drive(startForward, random(130, 190), startForward, 255);
    }
    delayWithSerial(random(650, 1200));
  }

  Serial.println(F("backup before turn"));
  drive(false, 245, false, 245);
  delayWithSerial(random(550, 950));

  if (random(0, 2) == 0) {
    Serial.println(F("turn left"));
    drive(true, turnSpeed, false, turnSpeed);
  } else {
    Serial.println(F("turn right"));
    drive(false, turnSpeed, true, turnSpeed);
  }
  delayWithSerial(random(450, 950));

  if (random(0, 100) < 25) {
    Serial.println(F("extra shove"));
    drive(random(0, 2) == 0, 255, random(0, 2) == 0, 255);
    delayWithSerial(random(180, 420));
  }

  stopMotors();
  delayWithSerial(random(20, 90));
}

void drive(bool rightForward, uint8_t rightSpeed, bool leftForward, uint8_t leftSpeed) {
  setMotor(RIGHT_MOTOR, rightForward, rightSpeed);
  setMotor(LEFT_MOTOR, leftForward, leftSpeed);
}

void setMotor(uint8_t motor, bool forward, uint8_t speed) {
  speed = constrain(speed, 0, 255);

  if (motor == RIGHT_MOTOR) {
    digitalWrite(PIN_MOTOR_STBY, HIGH);
    digitalWrite(PIN_RIGHT_DIRECTION, forward ? HIGH : LOW);
    analogWrite(PIN_RIGHT_SPEED, speed);
  } else {
    digitalWrite(PIN_MOTOR_STBY, HIGH);
    digitalWrite(PIN_LEFT_DIRECTION, forward ? HIGH : LOW);
    analogWrite(PIN_LEFT_SPEED, speed);
  }
}

void stopMotors() {
  analogWrite(PIN_LEFT_SPEED, 0);
  analogWrite(PIN_RIGHT_SPEED, 0);
  digitalWrite(PIN_LEFT_DIRECTION, LOW);
  digitalWrite(PIN_RIGHT_DIRECTION, LOW);
}

void delayWithSerial(unsigned int ms) {
  unsigned long started = millis();
  while (millis() - started < ms) {
    handleAlarmTrigger();
    handleSerial();
    if (!armed) {
      stopMotors();
      return;
    }

    delay(15);
  }
}

void handleSerial() {
  while (Serial.available() > 0) {
    char command = Serial.read();
    if (command == 's' || command == 'S') {
      armed = false;
      stopMotors();
      Serial.println(F("stopped"));
    } else if (command == 'r' || command == 'R') {
      armed = true;
      Serial.println(F("resumed"));
    }
  }
}

void handleAlarmTrigger() {
  bool triggerState = digitalRead(PIN_ALARM_TRIGGER);
  unsigned long now = millis();

  if (triggerState == HIGH && lastTriggerState == LOW &&
      now - lastTriggerChangeMs > TRIGGER_DEBOUNCE_MS) {
    armed = !armed;
    lastTriggerChangeMs = now;

    if (armed) {
      Serial.println(F("alarm trigger: running"));
    } else {
      stopMotors();
      Serial.println(F("alarm trigger: stopped"));
    }
  }

  lastTriggerState = triggerState;
}
