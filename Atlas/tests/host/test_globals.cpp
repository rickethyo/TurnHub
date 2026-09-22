#include <Arduino.h>
#include <WiFi.h>

uint32_t testNow = 1000;
int testDigitalRead = HIGH;
SerialStub Serial;
int loggedErrors = 0;
WiFiStub WiFi;
uint32_t testRandom = 12345;
EspStub ESP;
