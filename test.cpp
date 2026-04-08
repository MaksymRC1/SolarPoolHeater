#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 6
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

void setup() {
  Serial.begin(9600);
  sensors.begin();
  delay(1000);
  
  int count = sensors.getDeviceCount();
  Serial.print("Found ");
  Serial.print(count);
  Serial.println(" DS18B20 sensors");
}

void loop() {
  sensors.requestTemperatures();
  float t1 = sensors.getTempCByIndex(0);
  float t2 = sensors.getTempCByIndex(1);
  
  Serial.print("Sensor1: ");
  Serial.print(t1);
  Serial.print(" Sensor2: ");
  Serial.println(t2);
  
  delay(2000);
}