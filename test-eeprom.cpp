#include <Arduino.h>
#include <ArduinoJson.h>

byte count1 = 0;
byte count2 = 1;
char lineToken[44] = "1178568dafe78daf876dae9876dfa87fe89af867487";

int count1Addr = 0;
int count2Addr = 1;

void setup() {
    Serial.begin(115200);
}

void loop() {
    DynamicJsonDocument doc(1024);
    doc["sensor"] = "dht";
    doc["count1"] = 12;
    doc["count2"] = 23;
    doc["token"] = lineToken;
    String MSG;
    serializeJson(doc, MSG);
    Serial.println(MSG);
    delay(1000);
}