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

unsigned long bothPressStart = 0;
bool bothPressed = false;
bool switchingMode = false;

bool lastUpState = HIGH;
bool lastDownState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

unsigned long lastSensorRead = 0;
unsigned long lastDisplayUpdate = 0;

// ==================== ФУНКЦИИ ====================

void beep(int ms) {
  tone(BUZZER_PIN, 1000, ms);
  delay(ms);
}

void beepDouble() {
  beep(100);
  delay(100);
  beep(100);
}

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
    beep(50);
  } else {
    if (increment) {
      targetTemp += 0.5;
      if (targetTemp > 40.0) targetTemp = 40.0;
    } else {
      targetTemp -= 0.5;
      if (targetTemp < 18.0) targetTemp = 18.0;
    }
    beep(50);
  }
}

void switchScreen() {
  currentScreen = (currentScreen + 1) % 2;
  beep(50);
}

void switchMode() {
  currentMode = (currentMode == AUTO) ? MANUAL : AUTO;
  lcd.clear();
  lcd.print(currentMode == AUTO ? "AUTO MODE" : "MANUAL MODE");
  lcd.setCursor(0, 1);
  lcd.print(currentMode == AUTO ? "Delta T control" : "Target temp");
  beep(500);
  delay(1500);
  lcd.clear();
  currentScreen = 0;
}

void handleButtons() {
  bool up = !digitalRead(BUTTON_UP);
  bool down = !digitalRead(BUTTON_DOWN);
  
  if (up && down) {
    if (!bothPressed) {
      bothPressed = true;
      bothPressStart = millis();
    }
    
    if (!switchingMode && (millis() - bothPressStart >= 5000)) {
      switchingMode = true;
      switchMode();
      switchingMode = false;
      bothPressed = false;
    }
    return;
  }
  
  if (!up && !down && bothPressed && !switchingMode) {
    unsigned long pressDuration = millis() - bothPressStart;
    if (pressDuration < 5000 && pressDuration > debounceDelay) {
      switchScreen();
    }
    bothPressed = false;
  }
  
  if (!up && !down) {
    bothPressed = false;
    switchingMode = false;
  }
  
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

void updateDisplay() {
  if (currentScreen == 0) {
    lcd.clear();
    
    lcd.setCursor(0, 0);
    lcd.print("In:");
    lcd.print(tempIn, 1);
    lcd.print("C Out:");
    lcd.print(tempOut, 1);
    lcd.print("C");
    
    lcd.setCursor(0, 1);
    
    if (currentMode == AUTO) {
      lcd.print("AUTO ");
      lcd.print(pumpState ? "ON " : "OFF");
      lcd.print("dT:");
      lcd.print(deltaTemp, 1);
      lcd.print("/");
      lcd.print(deltaOn, 0);
    } else {
      lcd.print("MANUAL ");
      lcd.print(pumpState ? "ON " : "OFF");
      lcd.print("T:");
      lcd.print(tempIn, 1);
      lcd.print("/");
      lcd.print(targetTemp, 0);
    }
  } 
  else {
    lcd.clear();
    
    lcd.setCursor(0, 0);
    lcd.print("Outside: ");
    if (tempOutside > -100) {
      lcd.print(tempOutside, 1);
      lcd.print("C");
    } else {
      lcd.print("ERROR");
    }
    
    lcd.setCursor(0, 1);
    lcd.print("Humidity: ");
    if (humidity > 0) {
      lcd.print(humidity, 0);
      lcd.print("%");
    } else {
      lcd.print("ERROR");
    }
  }
}

// ==================== SETUP ====================
void setup() {
  lcd.begin(16, 2);
  Serial.begin(9600);
  
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  
  digitalWrite(RELAY_PIN, LOW);
  
  ds18b20.begin();
  dht.begin();
  
  // Диагностика DS18B20
  int deviceCount = ds18b20.getDeviceCount();
  Serial.print("Found ");
  Serial.print(deviceCount);
  Serial.println(" DS18B20 sensors");
  
  if (deviceCount == 0) {
    lcd.clear();
    lcd.print("No DS18B20!");
    while(1); // остановка
  }
  
  lcd.print("Solar Pool Heater");
  lcd.setCursor(0, 1);
  lcd.print("Summer Edition");
  delay(2000);
  
  lcd.clear();
  
  beep(200);
}

// ==================== LOOP ====================
void loop() {
  if (millis() - lastSensorRead > 1500) {
    ds18b20.requestTemperatures();
    tempIn = ds18b20.getTempCByIndex(0);
    tempOut = ds18b20.getTempCByIndex(1);
    deltaTemp = tempOut - tempIn;
    
    // Диагностика в Serial Monitor
    Serial.print("In: ");
    Serial.print(tempIn);
    Serial.print(" Out: ");
    Serial.println(tempOut);
    
    tempOutside = dht.readTemperature();
    humidity = dht.readHumidity();
    
    if (isnan(tempOutside)) tempOutside = -999;
    if (isnan(humidity)) humidity = -999;
    
    lastSensorRead = millis();
  }
  
  handleButtons();
  
  if (currentMode == AUTO) {
    if (!pumpState && deltaTemp >= deltaOn) {
      pumpState = true;
      digitalWrite(RELAY_PIN, HIGH);
      beep(100);
    }
    else if (pumpState && deltaTemp <= deltaOff) {
      pumpState = false;
      digitalWrite(RELAY_PIN, LOW);
      beep(100);
    }
  } else {
    if (!pumpState && tempIn < targetTemp - manualHysteresis) {
      pumpState = true;
      digitalWrite(RELAY_PIN, HIGH);
      beepDouble();
    }
    else if (pumpState && tempIn >= targetTemp) {
      pumpState = false;
      digitalWrite(RELAY_PIN, LOW);
      beepDouble();
    }
  }
  
  if (millis() - lastDisplayUpdate > 200) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  delay(50);
}