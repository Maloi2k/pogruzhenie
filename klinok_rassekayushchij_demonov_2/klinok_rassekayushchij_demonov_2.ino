/********************************************************
 * QUEST STEP SYSTEM v3.0
 * Arduino Mega
 * Управление квестом: шаги / подшаги
 ********************************************************/

#include <DFPlayerMini_Fast.h>

/* ================== НАСТРОЙКИ ================== */

// Кнопки радиореле (LOW)
#define PIN_OPEN 5
#define PIN_STEP 4
#define PIN_SUBSTEP 3
#define PIN_RESET 2

// Антидребезг
const unsigned long DEBOUNCE_DELAY = 150;

// Импульсы
#define MAX_PULSES 10

// Для РС-485 

const uint8_t PIN_RS485_DE_RE = 50; // DE и /RE вместе -> D50 

/* ================== РЕЛЕ ================== */

const uint8_t RELAYS[] = {6,  7,  8,  9,  10, 11, 12, 22, 23,
                          24, 25, 26, 27, 28, 29, 30, 31, 32};

const uint8_t RELAY_COUNT = sizeof(RELAYS) / sizeof(RELAYS[0]);

/* ================== ИМЕНА РЕЛЕ ================== */

enum RelayID {
  LIGHT,
  POZVON,
  GLAZ,
  KRIK,
  RELAY_10,
  RELAY_11,
  RELAY_12,
  OK4,
  LAMP,
  PROVOD,
  CHEM,
  OK3,
  OK2,
  BOX,
  OK1,
  DOOR4,
  DOOR2,
  DOOR1
};

/* ================== DFPLAYER ================== */

DFPlayerMini_Fast player1;
DFPlayerMini_Fast player2;

/* ================== СОСТОЯНИЕ ================== */

uint8_t currentStep = 0;
uint8_t currentSubstep = 0;

/* ================== СТРУКТУРЫ ================== */

struct Button {
  uint8_t pin;
  bool lastState;
  unsigned long lastChange;
};

struct Pulse {
  uint8_t relayIndex;
  unsigned long startTime;
  unsigned long duration;
  bool active;
};

/* ================== КНОПКИ ================== */
Button btnOpen = {PIN_OPEN, HIGH, 0};
Button btnStep = {PIN_STEP, HIGH, 0};
Button btnSubstep = {PIN_SUBSTEP, HIGH, 0};
Button btnReset = {PIN_RESET, HIGH, 0};

/* ================== ИМПУЛЬСЫ ================== */

Pulse pulses[MAX_PULSES];


// ====== МОРГАНИЕ LAMP (неблокирующее) ======
bool lampBlinkEnabled = false;
unsigned long lampBlinkInterval = 250;   // мс (половина периода)
unsigned long lampBlinkLastToggle = 0;
bool lampBlinkStateOn = false;
bool lampBlinkDelayed = false;
unsigned long lampBlinkStartTime = 0;

void startLampBlinkDelayed(unsigned long delayMs, unsigned long intervalMs) {
  lampBlinkDelayed = true;
  lampBlinkStartTime = millis() + delayMs;
  lampBlinkInterval = intervalMs;
}

void startLampBlink(unsigned long intervalMs) {
  lampBlinkEnabled = true;
  lampBlinkInterval = intervalMs;
  lampBlinkLastToggle = millis();
  lampBlinkStateOn = false;
  relayOff(LAMP); // стартуем с "выкл"
}

void stopLampBlink(bool leaveOn) {
  lampBlinkEnabled = false;
  if (leaveOn) relayOn(LAMP);
  else relayOff(LAMP);
}

void updateLampBlink() {

  // проверяем отложенный старт
  if (lampBlinkDelayed && millis() >= lampBlinkStartTime) {
    lampBlinkDelayed = false;
    startLampBlink(lampBlinkInterval);
  }

  if (!lampBlinkEnabled) return;

  unsigned long now = millis();
  if (now - lampBlinkLastToggle >= lampBlinkInterval) {
    lampBlinkLastToggle = now;
    lampBlinkStateOn = !lampBlinkStateOn;

    if (lampBlinkStateOn) relayOn(LAMP);
    else relayOff(LAMP);
  }
}
/* ================== SETUP ================== */

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);
  Serial2.begin(9600);
  Serial3.begin(9600);
  pinMode(PIN_OPEN, INPUT_PULLUP);
  pinMode(PIN_STEP, INPUT_PULLUP);
  pinMode(PIN_SUBSTEP, INPUT_PULLUP);
  pinMode(PIN_RESET, INPUT_PULLUP);

  pinMode(PIN_RS485_DE_RE, OUTPUT);
  rs485SetTransmit(false);

  // Инициализация всех реле
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    pinMode(RELAYS[i], OUTPUT);
    digitalWrite(RELAYS[i], HIGH);  // OFF
  }

  for (uint8_t i = 0; i < MAX_PULSES; i++) {
    pulses[i].active = false;
  }

  if (!player1.begin(Serial1, true)) {
    Serial.println("DFPlayer1 not detected!");
    // continue or halt as you like
  } else {
    player1.volume(25);
  }

  if (!player2.begin(Serial2, true)) {
    Serial.println("DFPlayer2 not detected!");
    // continue or halt as you like
  } else {
    player2.volume(25);
  }
  Serial.println("System ready.");
}

/* ================== LOOP ================== */

void loop() {
  checkButton(btnOpen, onOpenPressed);
  checkButton(btnStep, onStepPressed);
  checkButton(btnSubstep, onSubstepPressed);
  checkButton(btnReset, onResetPressed);

  handlePulses();
  updateLampBlink();
}

void rs485SetTransmit(bool on) {
  // DE=1 и /RE=1 (если /RE тоже на этом пине) => передача
  // DE=0 и /RE=0 => приём
  digitalWrite(PIN_RS485_DE_RE, on ? HIGH : LOW);
  // Небольшая пауза, чтобы MAX485 успел переключиться
  delayMicroseconds(20);
}

void rs485SendLine(const char* line) {
  rs485SetTransmit(true);
  Serial3.print(line);
  Serial3.print('\n');
  Serial.println(line);
  Serial.println('\n');
  Serial3.flush();              // дождаться окончания передачи
  delayMicroseconds(50);
  rs485SetTransmit(false);
}

/* ================== КНОПКИ ================== */

void checkButton(Button &btn, void (*callback)()) {
  bool reading = digitalRead(btn.pin);

  if (reading != btn.lastState) {
    btn.lastChange = millis();
    btn.lastState = reading;
  }

  if (reading == LOW && (millis() - btn.lastChange) > DEBOUNCE_DELAY) {
    callback();
    // ждём отпускания
    while (digitalRead(btn.pin) == LOW) {
      handlePulses();
    }
    btn.lastChange = millis();
  }
}

/* ================== ОБРАБОТЧИКИ ================== */
void onOpenPressed() {
  if (digitalRead(RELAYS[DOOR2]) == LOW) {
    relayOff(DOOR2); // было закрыто -> открываем
    Serial.println("Открыли дверь кондуктора");
  } else {
    relayOn(DOOR2);  // было открыто -> закрываем
    Serial.println("Закрыли дверь кондуктора");
  }
}

void onStepPressed() {
  currentStep++;
  currentSubstep = 0;
  runStep(currentStep);
}

void onSubstepPressed() {
  currentSubstep++;
  runSubstep(currentStep, currentSubstep);
}

void onResetPressed() { resetQuest(); }
void resetQuest() {
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    digitalWrite(RELAYS[i], HIGH);  // OFF
  }
  player1.stop();
  player2.stop();

  rs485SendLine("ALL STOP");

  relayOn(LAMP);
  relayOn(DOOR1);
  relayOn(DOOR2);
  relayOn(DOOR4);
  relayOn(LIGHT);
  relayOn(POZVON);
  relayOn(PROVOD);
  stopLampBlink(true);
  //
  currentStep = 0;
  currentSubstep = 0;
}
/* ================== УПРАВЛЕНИЕ РЕЛЕ ================== */

void relayOn(uint8_t index) {
  if (index >= RELAY_COUNT) return;
  digitalWrite(RELAYS[index], LOW);
}

void relayOff(uint8_t index) {
  if (index >= RELAY_COUNT) return;
  digitalWrite(RELAYS[index], HIGH);
}

void relayPulse(uint8_t index, unsigned long duration) {
  if (index >= RELAY_COUNT) return;

  for (uint8_t i = 0; i < MAX_PULSES; i++) {
    if (!pulses[i].active) {
      pulses[i] = {index, millis(), duration, true};
      digitalWrite(RELAYS[index], LOW);
      break;
    }
  }
}

void handlePulses() {
  for (uint8_t i = 0; i < MAX_PULSES; i++) {
    if (pulses[i].active) {
      if (millis() - pulses[i].startTime >= pulses[i].duration) {
        digitalWrite(RELAYS[pulses[i].relayIndex], HIGH);
        pulses[i].active = false;
      }
    }
  }
}

/* ================== ШАГИ ==================*/

void runStep(uint8_t step) {
  switch (step) {
    case 1:
      player1.play(1); // Музыка бриф
      break;

    case 2:
      player1.play(2); // Музыка 1 комната
      break;

    case 3:
      player2.play(1); // Звук гонга
      break;

    case 4:
      relayOff(DOOR1);
      player1.play(3); // Открываем дверь во вторую комнату + Фон 2 комната
      break;

    case 5:
      relayOff(LIGHT);
      player1.play(4); //Свет - холодный + тревожная музыка комната 2
      break;

    case 6:
      relayPulse(OK1, 300); // Открыли окно 1
      break;

    case 7:
      relayOn(GLAZ);
      player2.play(7); // Включились глаза и зацикленный звук не сопротивляйтесь
      break;

    case 8:
      relayPulse(OK4, 300); // Открыли окно 3
      break;

    case 9:
      player2.play(8);
      relayOn(CHEM); 
      relayOn(OK2);// Включаем чемоданы и стартуем трек у всего свое место
      break;

    case 10:
      player1.play(5); 
      relayOn(LIGHT);
      relayOff(OK2);
      rs485SendLine("1 PLAY 1 LOOP");  // Фон 3 комната + желтый свет во второй комнате + спящий чел на телевизоре
      break;

    case 11:
      rs485SendLine("1 PLAY 2"); // чел проснулся
      break;

    case 12:
      player2.play(12); // Зацикленный стук сердца
      break;

    case 13:
      player2.stop();
      relayOff(DOOR4);
      relayOff(PROVOD);
      player1.play(6); // Открылась дверь в последнюю комнату + Трек фон 4 комната + выключили стук сердца
      break;

    case 14:
      player1.play(8); // финальная музыка
      stopLampBlink(true);
      break;
  }
}

/* ================== ПОДШАГИ ==================*/

void runSubstep(uint8_t step, uint8_t sub) {

  if (step == 3) {
    switch (sub) {
      case 1:
        relayOn(BOX); // Активировали грушу
        break;

      case 2:
        player2.play(1); // звук Гонг
        break;

      case 3:
        player2.play(2); //звук Признание силы
        break;
    }
  }

  if (step == 4) {
    switch (sub) {
      case 1:
        player2.play(3); // звук Приготовьте билеты к проверке
        break;

      case 2:
        player2.play(4); // звук Дорога длинная (Длинная мантра чтоб заснуть)
        break;

      case 3:
        player2.stop(); // Остановка воспроизведения мантры
        break;
    }
  }

  if (step == 5) {
    switch (sub) {
      case 1:
        player2.play(5); //звук Все должны сесть согласно купленным билетам
        break;

      case 2:
        player2.play(6); //звук Склонись перед энму
        break;
    }
  }

  if (step == 7) {
    switch (sub) {
      case 1:
        player2.stop();
        relayPulse(OK3, 300);
        relayOff(GLAZ); // Глаза выключились окно 2 открылось, чел замолчал
        break;
    }
  }

  if (step == 10) {
    switch (sub) {
      case 1:
        relayOn(KRIK); //включили и  Откалибровали крикометр
        break;

      case 2:
        player2.play(9); // Он вас не слышит трек
        break;

      case 3:
        player2.play(10); // Такой слабый голос трек
        break;

      case 4:
        player2.play(11); // Кричи так громко трек
        break;
    }
  }

  if (step == 13) {
    switch (sub) {
      case 1:
        relayOff(POZVON); // Открыли позвоночник
        break;

      case 2:
        player1.play(7);
        startLampBlinkDelayed(18500, 1000); // Сирена + моргание света
        break;
    }
  }
}
