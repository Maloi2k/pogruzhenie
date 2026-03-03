#include <SoftwareSerial.h>

// RS-485 через MAX485
const uint8_t PIN_RS485_RX = 2;   // RO -> D2
const uint8_t PIN_RS485_TX = 3;   // DI -> D3
const uint8_t PIN_RS485_DE_RE = 4; // DE и /RE вместе -> D4

// Кнопки
const uint8_t BTN1 = 8;
const uint8_t BTN2 = 9;

// Настройки
const long BAUD = 9600;

// Дребезг
const unsigned long DEBOUNCE_MS = 40;

SoftwareSerial rs485(PIN_RS485_RX, PIN_RS485_TX);

bool lastBtn1 = HIGH;
bool lastBtn2 = HIGH;
unsigned long lastChange1 = 0;
unsigned long lastChange2 = 0;

void rs485SetTransmit(bool on) {
  // DE=1 и /RE=1 (если /RE тоже на этом пине) => передача
  // DE=0 и /RE=0 => приём
  digitalWrite(PIN_RS485_DE_RE, on ? HIGH : LOW);
  // Небольшая пауза, чтобы MAX485 успел переключиться
  delayMicroseconds(20);
}

void rs485SendLine(const char* line) {
  rs485SetTransmit(true);
  rs485.print(line);
  rs485.print('\n');
  rs485.flush();              // дождаться окончания передачи
  delayMicroseconds(50);
  rs485SetTransmit(false);
}

void setup() {
  pinMode(PIN_RS485_DE_RE, OUTPUT);
  rs485SetTransmit(false);

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);

  rs485.begin(BAUD);

  // (опционально) отладка по USB:
  // Serial.begin(115200);
  // Serial.println("RS485 ready");
}

void loop() {
  // --- Кнопка 1 ---
  bool b1 = digitalRead(BTN1);
  if (b1 != lastBtn1) {
    lastChange1 = millis();
    lastBtn1 = b1;
  }
  if (b1 == LOW && (millis() - lastChange1) > DEBOUNCE_MS) {
    // чтобы отправить команду один раз на нажатие — ждём отпускания
    rs485SendLine("PLAY1");
    while (digitalRead(BTN1) == LOW) {
      delay(5);
    }
    // обновим состояние после отпускания
    lastBtn1 = HIGH;
    lastChange1 = millis();
  }

  // --- Кнопка 2 ---
  bool b2 = digitalRead(BTN2);
  if (b2 != lastBtn2) {
    lastChange2 = millis();
    lastBtn2 = b2;
  }
  if (b2 == LOW && (millis() - lastChange2) > DEBOUNCE_MS) {
    rs485SendLine("PLAY2");
    while (digitalRead(BTN2) == LOW) {
      delay(5);
    }
    lastBtn2 = HIGH;
    lastChange2 = millis();
  }

  // Небольшая пауза
  delay(5);
}