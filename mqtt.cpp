const char version[6] = "0.1.0";

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <TridentTD_LineNotify.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#define MQTT_HOST "bzcom.ddns.net"

// const char *ssid     = "R&D_2";
// const char *password = "1qaz7ujmrd2";

const char *ssid     = "BZ_IOT";
const char *password = "Password";

IPAddress local_IP(192, 168, 1, 28);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 1, 1);
IPAddress secondaryDNS(8, 8, 8, 8);
// IPAddress secondaryDNS(192, 168, 1, 3);

WiFiClient espClient;
PubSubClient client(espClient);
AsyncWebServer server(80);

bool pumpState = 0;
bool lightState = 0;
bool useLineNotify = 0;
// bool pumpManualActive = 0;
// byte pumpManualTimeCount = 0;
// byte pumpOnDuration = 10;
// byte pumpOffDuration = 10;
char lineToken[44] = "";

// EEPROM Address
byte pumpStateAddr = 0;
byte lightStateAddr = 1;
byte useLineNotifyAddr = 2;
// byte pumpOnDurationAddr = 3;
// byte pumpOffDurationAddr = 4;
// byte lowLimitMoisAddr = 5;
byte lineTokenAddr = 8;

unsigned long mqttInfo;
unsigned long mqttInfoChart;
unsigned long mqttReconnTime;

void sendLine(String msg) {
	if (strlen(lineToken) == 43 && useLineNotify == 1) {
		LINE.notify(msg);
	}
}

void callback(char* topic, byte* message, unsigned int length) {
    String messageTemp;

    for (int i = 0; i < length; i++) {
        Serial.print((char)message[i]);
        messageTemp += (char)message[i];
    }

    DynamicJsonDocument doc(1024);
	DynamicJsonDocument docSend(256);
    deserializeJson(doc, messageTemp);
	Serial.println(messageTemp);
    
    if (String(topic) == "esp/watering/get") {
		bool flagLight = doc["light"]["flag"];
		if (flagLight == 1) {
			bool getLight = doc["light"]["state"];
			lightState = getLight;
			EEPROM.put(lightStateAddr, lightState);
			EEPROM.commit();
			String lightStateStr;
			if (lightState == 0) lightStateStr = "OFF";
			else if (lightState == 1) lightStateStr = "ON";
			String MSG = "\nNotify - Light\nLight " + lightStateStr;
			sendLine(MSG);
			DynamicJsonDocument docSend(64);
			docSend["status"] = "OK";
			docSend["message"] = "Saved light state";
			String MQTT_STR;
			serializeJson(docSend, MQTT_STR);
			client.publish("esp/watering/savestatus", MQTT_STR.c_str());
		}

		bool flagUseLineNotify = doc["line"]["act"]["flag"];
		if (flagUseLineNotify == 1) {
			bool getUseLineNotify = doc["line"]["act"]["status"];
			useLineNotify = getUseLineNotify;
			EEPROM.put(useLineNotifyAddr, useLineNotify);
			EEPROM.commit();
			String useLineNotifyStr;
			if (useLineNotify == 0) useLineNotifyStr = "Not use";
			else if (useLineNotify == 1) useLineNotifyStr = "Use";
			String MSG = "\nNotify - LINE Notify\n" + useLineNotifyStr;
			sendLine(MSG);
			DynamicJsonDocument docSend(64);
			docSend["status"] = "OK";
			docSend["message"] = "Saved Line Notify setting";
			String MQTT_STR;
			serializeJson(docSend, MQTT_STR);
			client.publish("esp/watering/savestatus", MQTT_STR.c_str());
		}

		bool flagLineToken = doc["line"]["token"]["flag"];
		if (flagLineToken == 1) {
			String getLineToken = doc["line"]["token"]["token"];
			Serial.println(getLineToken);
			getLineToken.toCharArray(lineToken, 44);
			DynamicJsonDocument docSend(64);
			if (strlen(lineToken) == 43) {
				EEPROM.put(lineTokenAddr, lineToken);
				EEPROM.commit();
				sendLine("\nLINE Notify token changed!");
				LINE.setToken(lineToken);
				docSend["status"] = "OK";
				docSend["message"] = "Saved Line Token";
				docSend["lineToken"] = String(lineToken);
			} else {
				docSend["status"] = "ERROR";
				docSend["message"] = "Invalid Line Token";
			}
			String MQTT_STR;
			serializeJson(docSend, MQTT_STR);
			client.publish("esp/watering/savestatus", MQTT_STR.c_str());
		}
	}
}

String convState(int state) {
	if (state == 0) return "OFF";
	else if (state == 1) return "ON ";
	else return "E  ";
}
void connectToWifi() {
	Serial.println("Connecting to Wi-Fi...");
	if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
		Serial.println("STA Failed to configure");
	}
	WiFi.begin(ssid, password);
	WiFi.setAutoReconnect(true);
	WiFi.persistent(true);
	Serial.print("Connected to ");
	Serial.println(WiFi.SSID());
}

void reconnect() {
    if (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (client.connect("ESP8266Client")) {
            Serial.println("connected");
            client.subscribe("esp/watering/get");
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
        }
    }
}

void setup() {
	Serial.begin(115200);
	EEPROM.begin(256);
	connectToWifi();
	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Hi! I am ESP8266. ESP-Test\nVersion: " + String(version));
	});
	
	AsyncElegantOTA.begin(&server);         // Start ElegantOTA
    server.begin();                         // ElegantOTA
	client.setServer(MQTT_HOST, 1883);
	client.setCallback(callback);
	EEPROM.get(pumpStateAddr, pumpState);
	EEPROM.get(lightStateAddr, lightState);
	EEPROM.get(useLineNotifyAddr, useLineNotify);
	EEPROM.get(lineTokenAddr, lineToken);
	LINE.setToken(lineToken);
}

void loop() {
	client.loop();
	

	if (!client.connected()) {
		if (millis() - mqttReconnTime > 1000) {
			reconnect();
			mqttReconnTime = millis();
		}
	} else {
		DynamicJsonDocument docInfo(256);
		if (millis() - mqttInfo > 1000) {
			docInfo["lightState"] = String(lightState);
			docInfo["pumpState"] = String(pumpState);
			docInfo["useLine"] = String(useLineNotify);
			docInfo["rssi"] = String(WiFi.RSSI());
			docInfo["uptime"] = String(millis()/1000);
			String MQTT_STR;
			serializeJson(docInfo, MQTT_STR);
			client.publish("esp/watering/info", MQTT_STR.c_str());
			mqttInfo = millis();
		}
		// if (millis() - mqttInfoChart > 300000) { // 5 minutes
		// 	String MQTT_STR;
		// 	serializeJson(docInfo, MQTT_STR);
		// 	client.publish("esp/test/info", MQTT_STR.c_str());
		// 	mqttInfoChart = millis();
		// }
	}
}
