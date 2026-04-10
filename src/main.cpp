#include <Arduino.h>
#include <LiquidCrystal.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>

// ==================== ПИНЫ ====================
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

#define ONE_WIRE_BUS 6
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);

#define DHT_PIN 7
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

#define RELAY_PIN 8
#define BUTTON_UP 9
#define BUTTON_DOWN 10
#define BUZZER_PIN 13

// ==================== НАСТРОЙКИ ====================
enum Mode { AUTO, MANUAL };
Mode currentMode = AUTO;

float deltaOn = 3.0;
float deltaOff = 1.0;

float targetTemp = 28.0;
float manualHysteresis = 0.5;

float tempIn = 0;
float tempOut = 0;
float deltaTemp = 0;
float tempOutside = 0;
float humidity = 0;
bool pumpState = false;

int currentScreen = 0;

// Переменные для одновременного нажатия
unsigned long bothPressStart = 0;
bool bothPressed = false;
bool bothHandled = false;

// Переменные для отдельных кнопок
bool lastUpState = HIGH;
bool lastDownState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// Переменные для Hard Manual режима
bool hardManualMode = false;
bool hardManualPumpState = false;
unsigned long hardManualPressStart = 0;
bool hardManualPressed = false;

unsigned long lastSensorRead = 0;
unsigned long lastDisplayUpdate = 0;

// ==================== НОТЫ ДЛЯ ЗУММЕРА ====================
#define NOTE_C4  262
#define NOTE_E4  330
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_C5  523

void playTone(int frequency, int duration) {
  tone(BUZZER_PIN, frequency, duration);
  delay(duration + 50);
}

void playStartupMelody() {
  playTone(NOTE_C4, 200);
  playTone(NOTE_E4, 200);
  playTone(NOTE_G4, 200);
  playTone(NOTE_C5, 400);
  delay(100);
  playTone(NOTE_G4, 200);
  playTone(NOTE_E4, 200);
  playTone(NOTE_C4, 400);
}

void playPumpOnMelody() {
  playTone(NOTE_E4, 100);
  playTone(NOTE_G4, 100);
  playTone(NOTE_C5, 200);
}

void playPumpOffMelody() {
  playTone(NOTE_C5, 100);
  playTone(NOTE_G4, 100);
  playTone(NOTE_E4, 200);
}

void playModeSwitchMelody() {
  playTone(NOTE_G4, 150);
  playTone(NOTE_A4, 150);
  playTone(NOTE_C5, 300);
}

void playErrorMelody() {
  playTone(NOTE_G4, 200);
  playTone(NOTE_E4, 200);
  playTone(NOTE_C4, 400);
}

void beepShort() {
  playTone(NOTE_C4, 50);
}

// ==================== ФУНКЦИИ УПРАВЛЕНИЯ ====================
void changeParameter(bool increment) {
  if (currentMode == AUTO) {
    if (increment) {
      deltaOn += 0.5;
      if (deltaOn > 8.0) deltaOn = 8.0;
    } else {
      deltaOn -= 0.5;
      if (deltaOn < 1.0) deltaOn = 1.0;
    }
    deltaOff = deltaOn - 2.0;
    if (deltaOff < 0.5) deltaOff = 0.5;
    beepShort();
  } else {
    if (increment) {
      targetTemp += 0.5;
      if (targetTemp > 40.0) targetTemp = 40.0;
    } else {
      targetTemp -= 0.5;
      if (targetTemp < 18.0) targetTemp = 18.0;
    }
    beepShort();
  }
}

void switchScreen() {
  currentScreen = (currentScreen + 1) % 2;
  beepShort();
}

void switchMode() {
  currentMode = (currentMode == AUTO) ? MANUAL : AUTO;
  lcd.clear();
  lcd.print(currentMode == AUTO ? "AUTO MODE" : "MANUAL MODE");
  lcd.setCursor(0, 1);
  lcd.print(currentMode == AUTO ? "Delta T control" : "Target temp");
  playModeSwitchMelody();
  delay(1500);
  lcd.clear();
  currentScreen = 0;
}

// ==================== ОБРАБОТКА КНОПОК ====================
void handleButtons() {
  bool up = !digitalRead(BUTTON_UP);
  bool down = !digitalRead(BUTTON_DOWN);
  
  // ========== HARD MANUAL MODE (принудительное управление) ==========
  if (hardManualMode) {
    static unsigned long lastHardDisplay = 0;
    if (millis() - lastHardDisplay > 500) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("HARD MANUAL");
      lcd.setCursor(0, 1);
      lcd.print(hardManualPumpState ? "PUMP: ON " : "PUMP: OFF");
      lastHardDisplay = millis();
    }
    return;
  }
  
  // Проверка на вход в Hard Manual (долгое нажатие "+" 5 сек)
  if (up && !down) {
    if (!hardManualPressed) {
      hardManualPressed = true;
      hardManualPressStart = millis();
    }
    
    if (!hardManualMode && (millis() - hardManualPressStart >= 5000)) {
      hardManualMode = true;
      hardManualPumpState = true;
      digitalWrite(RELAY_PIN, HIGH);
      playModeSwitchMelody();
      lcd.clear();
      lcd.print("HARD MANUAL");
      lcd.setCursor(0, 1);
      lcd.print("PUMP: ON ");
      delay(1500);
      return;
    }
  }
  // Проверка на вход в Hard Manual (долгое нажатие "-" 5 сек)
  else if (down && !up) {
    if (!hardManualPressed) {
      hardManualPressed = true;
      hardManualPressStart = millis();
    }
    
    if (!hardManualMode && (millis() - hardManualPressStart >= 5000)) {
      hardManualMode = true;
      hardManualPumpState = false;
      digitalWrite(RELAY_PIN, LOW);
      playErrorMelody();
      lcd.clear();
      lcd.print("HARD MANUAL");
      lcd.setCursor(0, 1);
      lcd.print("PUMP: OFF");
      delay(1500);
      return;
    }
  } else {
    hardManualPressed = false;
  }
  
  // ========== ОБЕ КНОПКИ НАЖАТЫ ==========
  if (up && down) {
    if (!bothPressed) {
      bothPressed = true;
      bothPressStart = millis();
      bothHandled = false;
    }
    
    if (!bothHandled && (millis() - bothPressStart >= 5000)) {
      bothHandled = true;
      switchMode();
    }
    return;
  }
  
  // ========== КНОПКИ ОТПУЩЕНЫ ==========
  if (!up && !down && bothPressed && !bothHandled) {
    unsigned long pressDuration = millis() - bothPressStart;
    if (pressDuration < 5000 && pressDuration > debounceDelay) {
      switchScreen();
    }
    bothPressed = false;
  }
  
  if (!up && !down) {
    bothPressed = false;
    bothHandled = false;
  }
  
  // ========== ОТДЕЛЬНЫЕ КНОПКИ ==========
  if (up && !down && !bothPressed) {
    if (!lastUpState && (millis() - lastDebounceTime > debounceDelay)) {
      changeParameter(true);
      lastDebounceTime = millis();
    }
  }
  
  if (down && !up && !bothPressed) {
    if (!lastDownState && (millis() - lastDebounceTime > debounceDelay)) {
      changeParameter(false);
      lastDebounceTime = millis();
    }
  }
  
  lastUpState = up;
  lastDownState = down;
}

// ==================== ОБНОВЛЕНИЕ ДИСПЛЕЯ ====================
void updateDisplay() {
  static float lastTempIn = 0, lastTempOut = 0;
  static float lastDeltaTemp = 0;
  static float lastTempOutside = 0, lastHumidity = 0;
  static float lastDeltaOn = 0, lastTargetTemp = 0;
  static int lastPumpState = -1, lastMode = -1;
  static int lastScreen = -1;
  
  if (hardManualMode) return;
  
  if (currentScreen == 0) {
    if (lastScreen != 0) {
      lcd.clear();
      
      lcd.setCursor(0, 0);
      lcd.print("In:");
      lcd.setCursor(3, 0);
      lcd.print(tempIn, 1);
      lcd.setCursor(7, 0);
      lcd.print("Out:");
      lcd.setCursor(11, 0);
      lcd.print(tempOut, 1);
      
      lcd.setCursor(0, 1);
      lcd.print(currentMode == AUTO ? "A" : "M");
      lcd.setCursor(1, 1);
      lcd.print(pumpState ? "[ON]" : "[OFF]");
      lcd.setCursor(6, 1);
      lcd.print("dT");
      lcd.setCursor(8, 1);
      lcd.print(deltaTemp < 0 ? 0 : deltaTemp, 1);
      lcd.setCursor(12, 1);
      lcd.print("/");
      lcd.setCursor(13, 1);
      lcd.print(deltaOn, 0);
      
      lastScreen = 0;
      lastTempIn = lastTempOut = -999;
      lastDeltaTemp = -999;
      lastDeltaOn = -999;
      lastTargetTemp = -999;
      lastPumpState = -1;
      lastMode = -1;
    }
    
    if (abs(tempIn - lastTempIn) > 0.05) {
      lcd.setCursor(3, 0);
      lcd.print("   ");
      lcd.setCursor(3, 0);
      lcd.print(tempIn, 1);
      lastTempIn = tempIn;
    }
    
    if (abs(tempOut - lastTempOut) > 0.05) {
      lcd.setCursor(11, 0);
      lcd.print("   ");
      lcd.setCursor(11, 0);
      lcd.print(tempOut, 1);
      lastTempOut = tempOut;
    }
    
    if (currentMode != lastMode) {
      lcd.setCursor(0, 1);
      lcd.print(currentMode == AUTO ? "A" : "M");
      lastMode = currentMode;
    }
    
    if (pumpState != lastPumpState) {
      lcd.setCursor(1, 1);
      lcd.print(pumpState ? "[ON]" : "[OFF]");
      lastPumpState = pumpState;
    }
    
    if (currentMode == AUTO) {
      float displayDelta = deltaTemp < 0 ? 0 : deltaTemp;
      if (abs(displayDelta - lastDeltaTemp) > 0.05) {
        lcd.setCursor(8, 1);
        lcd.print("   ");
        lcd.setCursor(8, 1);
        lcd.print(displayDelta, 1);
        lastDeltaTemp = displayDelta;
      }
      
      if (abs(deltaOn - lastDeltaOn) > 0.05) {
        lcd.setCursor(13, 1);
        lcd.print(" ");
        lcd.setCursor(13, 1);
        lcd.print(deltaOn, 0);
        lastDeltaOn = deltaOn;
      }
    } else {
      if (abs(targetTemp - lastTargetTemp) > 0.05) {
        lcd.setCursor(8, 1);
        lcd.print("   ");
        lcd.setCursor(8, 1);
        lcd.print(targetTemp, 0);
        lastTargetTemp = targetTemp;
      }
    }
  } 
  else {
    if (lastScreen != 1) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Out:");
      lcd.setCursor(4, 0);
      lcd.print(tempOutside, 1);
      lcd.setCursor(9, 0);
      lcd.print("C");
      lcd.setCursor(0, 1);
      lcd.print("Hum:");
      lcd.setCursor(4, 1);
      lcd.print(humidity, 0);
      lcd.setCursor(8, 1);
      lcd.print("%");
      
      lastScreen = 1;
      lastTempOutside = -999;
      lastHumidity = -999;
    }
    
    if (abs(tempOutside - lastTempOutside) > 0.1 && tempOutside > -100) {
      lcd.setCursor(4, 0);
      lcd.print("    ");
      lcd.setCursor(4, 0);
      lcd.print(tempOutside, 1);
      lastTempOutside = tempOutside;
    }
    
    if (abs(humidity - lastHumidity) > 0.5 && humidity > 0) {
      lcd.setCursor(4, 1);
      lcd.print("   ");
      lcd.setCursor(4, 1);
      lcd.print(humidity, 0);
      lastHumidity = humidity;
    }
  }
}

// ==================== SETUP ====================
void setup() {
  lcd.begin(16, 2);
  
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  
  digitalWrite(RELAY_PIN, LOW);
  
  ds18b20.begin();
  dht.begin();
  
  lcd.print("Pool Heater");
  lcd.setCursor(0, 1);
  lcd.print("Summer Edition");
  delay(2000);
  
  lcd.clear();
  
  playStartupMelody();
}

// ==================== LOOP ====================
void loop() {
  if (millis() - lastSensorRead > 1500) {
    ds18b20.requestTemperatures();
    tempIn = ds18b20.getTempCByIndex(0);
    tempOut = ds18b20.getTempCByIndex(1);
    deltaTemp = tempOut - tempIn;
    
    tempOutside = dht.readTemperature();
    humidity = dht.readHumidity();
    
    if (isnan(tempOutside)) tempOutside = -999;
    if (isnan(humidity)) humidity = -999;
    
    lastSensorRead = millis();
  }
  
  handleButtons();
  
  // Если в режиме Hard Manual — блокируем обычную логику
  if (hardManualMode) {
    delay(100);
    return;
  }
  
  // ========== ОБЫЧНАЯ ЛОГИКА УПРАВЛЕНИЯ ==========
  if (currentMode == AUTO) {
    if (!pumpState && deltaTemp >= deltaOn) {
      pumpState = true;
      digitalWrite(RELAY_PIN, HIGH);
      playPumpOnMelody();
    }
    else if (pumpState && deltaTemp <= deltaOff) {
      pumpState = false;
      digitalWrite(RELAY_PIN, LOW);
      playPumpOffMelody();
    }
  } else {
    if (!pumpState && tempIn < targetTemp - manualHysteresis) {
      pumpState = true;
      digitalWrite(RELAY_PIN, HIGH);
      playPumpOnMelody();
    }
    else if (pumpState && tempIn >= targetTemp) {
      pumpState = false;
      digitalWrite(RELAY_PIN, LOW);
      playPumpOffMelody();
    }
  }
  
  if (millis() - lastDisplayUpdate > 200) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  delay(50);
}