/*
== ESP - Plant Watering System 0.2.1 ==
A0 -- Moisture Sensor
D0 -- DHT11
D1 -- SCL		(LCD)
D2 -- SDA		(LCD)
D4 -- RST		(DS1302)
D5 -- DAT		(DS1302)
D6 -- CLK		(DS1302)
D3 -- Relay 1	(Pump)
D7 -- Relay 2	(Light)
*/

const char version[6] = "0.5.0";

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <DS1302.h>
#include <TimeLib.h>
#include <LiquidCrystal_I2C.h>
#include <TridentTD_LineNotify.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

const byte MOISTURE_pin			= A0;
const byte DHT_pin				= D0;
const byte PUMP_pin				= D3;
const byte LIGHT_pin			= D7;
DS1302 rtc(D4, D5, D6);

// #define MQTT_HOST IPAddress(192, 168, 1, 3)
#define MQTT_HOST "bzcom.ddns.net"

Time t;

LiquidCrystal_I2C lcd(0x27, 16, 2);

const char *ssid     = "BZ_IOT";
const char *password = "Password";

WiFiClient espClient;
PubSubClient client(espClient);
AsyncWebServer server(80);
DHT dht;

byte dsHr = 0;
byte dsMin = 0;
byte dsSec = 0;
byte dsHrOnes = 0;
byte dsHrTens = 0;
byte dsMinOnes = 0;
byte dsMinTens = 0;
byte dsSecOnes = 0;
byte dsSecTens = 0;
byte temperature = 0;
byte humidity = 0;
bool firstSend = 0;
bool pumpState = 0;
bool lightState = 0;
bool useLineNotify = 0;
bool pumpManualActive = 0;
byte pumpManualTimeCount = 0;
byte pumpOnDelay = 10;
byte pumpOffDelay = 10;
char lineToken[44] = "";
byte viewPage = 1;
unsigned int moisValue = 0;
unsigned int lowLimitMois = 300;
char message_buff[100];
const char *prefixtopic = "esp/watering/";
const char *subscribetopic[] = {
	"pump/set",
	"pump/manual",
	"light",
	"line/act",
	"line/token",
	"mois",
	"time"};

// EEPROM Address
byte pumpStateAddr = 0;
byte lightStateAddr = 1;
byte useLineNotifyAddr = 2;
byte pumpOnDelayAddr = 3;
byte pumpOffDelayAddr = 4;
byte lowLimitMoisAddr = 5;
byte lineTokenAddr = 8;

unsigned long mqttInfo;
unsigned long mqttInfoChart;
unsigned long mqttReconnTime;
unsigned long pumpOnStoreTime;
unsigned long pumpOffStoreTime;
unsigned long pumpManualActiveTime;
unsigned long updateTimeDHT;
unsigned long viewPageChangeTime;
unsigned long moisReadTime;
unsigned long updateStatus;

void getTime() {
	t = rtc.getTime();
	dsHr = t.hour;
	dsMin = t.min;
	dsSec = t.sec;
	dsHrOnes = dsHr % 10;
	dsHrTens = dsHr / 10;
	dsMinOnes = dsMin % 10;
	dsMinTens = dsMin / 10;
	dsSecOnes = dsSec % 10;
	dsSecTens = dsSec / 10;
}

void getDht() {
	temperature = dht.getTemperature();
	humidity = dht.getHumidity();
}

void sendLine(String msg) {
	if (strlen(lineToken) == 43 && useLineNotify == 1) {
		LINE.notify(msg);
	}
}

void setLight(bool state) {
	if (state == 0) digitalWrite(LIGHT_pin, HIGH);
	else digitalWrite(LIGHT_pin, LOW);
}

void setPump(bool state) {
	if (pumpManualActive == 0) {
		if (state == 0) digitalWrite(PUMP_pin, HIGH);
		else digitalWrite(PUMP_pin, LOW);
	}
}

void callback(char* topic, byte* payload, unsigned int length) {
    // String message;

    // for (int i = 0; i < length; i++) {
    //     Serial.print((char)payload[i]);
    //     messageTemp += (char)message[i];
    // }
	// Serial.println();

    DynamicJsonDocument doc(256);
    deserializeJson(doc, payload, length);
	// Serial.println(messageTemp);
	int i;
	for (i = 0; i < length; i++) {
		message_buff[i] = payload[i];
		message_buff[i] = '\0';
	}
	String msgString = String(message_buff);
    
    if (strcmp(topic, "pump/set") == 0) {
		bool flagPumpSetting = doc["flag"];
		if (flagPumpSetting == 1) {
			byte getPumpOnDelay = doc["on"];
			byte getPumpOffDelay = doc["off"];
			pumpOnDelay = getPumpOnDelay;
			pumpOffDelay = getPumpOffDelay;
			EEPROM.put(pumpOnDelayAddr, pumpOnDelay);
			EEPROM.put(pumpOffDelayAddr, pumpOffDelay);
			EEPROM.commit();
			String MSG = "\nNotify - Setting\nPump on/off delay (" + String(pumpOnDelay) + "/" + String(pumpOffDelay) + ")";
			sendLine(MSG);
			// DynamicJsonDocument docSend(64);
			// docSend["status"] = "OK";
			// docSend["message"] = "Saved pump on/off delay (" + String(pumpOnDelay) + "/" + String(pumpOffDelay) + ")";
			// String MQTT_STR;
			// serializeJson(docSend, MQTT_STR);
			// client.publish("esp/watering/savestatus", MQTT_STR.c_str());
		}
	}

	if (strcmp(topic, "pump/manual") == 0) {
		bool flagManualPump = doc["flag"];
		if (flagManualPump == 1) {
			pumpManualActive = 1;
			String MSG = "\nNotify - Manual pump\nPump on (" + String(dsHr) + ":" + String(dsMin) + ")";
			sendLine(MSG);
			// DynamicJsonDocument docSend(64);
			// docSend["status"] = "OK";
			// docSend["message"] = "Activated manual pump on (" + String(dsHr) + ":" + String(dsMin) + ")";
			// String MQTT_STR;
			// serializeJson(docSend, MQTT_STR);
			// client.publish("esp/watering/savestatus", MQTT_STR.c_str());
		}
	}

	if (strcmp(topic, "light") == 0) {
		bool flagLight = doc["flag"];
		if (flagLight == 1) {
			bool getLight = doc["state"];
			if (getLight != lightState) {
				lightState = getLight;
				EEPROM.put(lightStateAddr, lightState);
				EEPROM.commit();
				setLight(lightState);
				String lightStateStr;
				if (lightState == 0)		lightStateStr = "OFF";
				else if (lightState == 1)	lightStateStr = "ON";
				String MSG = "\nNotify - Light\nLight " + lightStateStr;
				sendLine(MSG);
				// DynamicJsonDocument docSend(64);
				// docSend["status"] = "OK";
				// docSend["message"] = "Saved light state (" + lightStateStr + ")";
				// String MQTT_STR;
				// serializeJson(docSend, MQTT_STR);
				// client.publish("esp/watering/savestatus", MQTT_STR.c_str());
			}
		}
	}

	if (strcmp(topic, "line/act") == 0) {
		bool flagUseLineNotify = doc["flag"];
		if (flagUseLineNotify == 1) {
			bool getUseLineNotify = doc["state"];
			if (getUseLineNotify != useLineNotify) {
				useLineNotify = getUseLineNotify;
				EEPROM.put(useLineNotifyAddr, useLineNotify);
				EEPROM.commit();
				String useLineNotifyStr;
				if (useLineNotify == 0)			useLineNotifyStr = "Not use";
				else if (useLineNotify == 1)	useLineNotifyStr = "Use";
				String MSG = "\nNotify - LINE Notify\n" + useLineNotifyStr;
				sendLine(MSG);
				// DynamicJsonDocument docSend(64);
				// docSend["status"] = "OK";
				// docSend["message"] = "Saved Line Notify setting (" + useLineNotifyStr + ")";
				// String MQTT_STR;
				// serializeJson(docSend, MQTT_STR);
				// client.publish("esp/watering/savestatus", MQTT_STR.c_str());
			}
		}
	}

	if (strcmp(topic, "line/token") == 0) {
		bool flagLineToken = doc["flag"];
		if (flagLineToken == 1) {
			String getLineToken = doc["token"];
			Serial.println(getLineToken);
			getLineToken.toCharArray(lineToken, 44);
			// DynamicJsonDocument docSend(64);
			if (strlen(lineToken) == 43) {
				EEPROM.put(lineTokenAddr, lineToken);
				EEPROM.commit();
				sendLine("\nLINE Notify token changed!");
				LINE.setToken(lineToken);
			}
				// docSend["status"] = "OK";
				// docSend["message"] = "Saved Line Token";
				// docSend["lineToken"] = String(lineToken);
			// } else {
			// 	docSend["status"] = "ERROR";
			// 	docSend["message"] = "Invalid Line Token";
			// }
			// String MQTT_STR;
			// serializeJson(docSend, MQTT_STR);
			// client.publish("esp/watering/savestatus", MQTT_STR.c_str());
		}
	}

	if (strcmp(topic, "mois") == 0) {
		bool flagLowLimitMois = doc["flag"];
		if (flagLowLimitMois == 1) {
			byte getLowLimitMois = doc["value"];
			lowLimitMois = getLowLimitMois;
			EEPROM.put(lowLimitMoisAddr, lowLimitMois);
			EEPROM.commit();
			String MSG = "\nNotify - LINE Notify\nMoisture low limit = " + String(lowLimitMois*10);
			sendLine(MSG);
			// DynamicJsonDocument docSend(64);
			// docSend["status"] = "OK";
			// docSend["message"] = "Saved Low limit moisture (" + String(lowLimitMoisture) + ")";
			// String MQTT_STR;
			// serializeJson(docSend, MQTT_STR);
			// client.publish("esp/watering/savestatus", MQTT_STR.c_str());
		}
	}

	if (strcmp(topic, "time") == 0) {
		bool flagUpdateTime = doc["flag"];
        if (flagUpdateTime == 1) {
			byte Second	= doc["time"]["ss"];
			byte Minute	= doc["time"]["mm"];
			byte Hour	= doc["time"]["HH"];
			byte Day	= doc["time"]["dd"];
			byte Month	= doc["time"]["MM"];
			int Year	= doc["time"]["yyyy"];
			if (Hour != dsHr || Minute != dsMin) {
				rtc.setTime(Hour, Minute, Second);
				rtc.setDate(Day, Month, Year);
			}
			// DynamicJsonDocument docSend(64);
            // String MQTT_STR;
			// docSend["status"] = "OK";
			// docSend["message"] = "updated";
			// serializeJson(docSend, MQTT_STR);
            // client.publish("esp/watering/savestatus", MQTT_STR.c_str());
        }
	}
}

void manualPumpCheck() {
	if (pumpManualActive == 1) {
		if (millis() - pumpManualActiveTime > 1000) {
			pumpManualTimeCount = pumpManualTimeCount+1;
			if (pumpManualTimeCount > 10) {
				pumpManualTimeCount = 0;
				pumpManualActive = 0;
				setPump(0);
			} else {
				setPump(1);
			}
			pumpManualActiveTime = millis();
		}
	}
}

String convState(int state) {
	if (state == 0) return "OFF ";
	else if (state == 1) return "ON  ";
	else return "E   ";
}

String delayStr(int num) {
	String str;
	if (num < 10)		str = String(num) + "  ";
	else if (num < 100) str = String(num) + " ";
	return str;
}

void pinDisp() {
	String time = String(dsHrTens) + String(dsHrOnes)
		+ ":" + String(dsMinTens) + String(dsMinOnes)
		+ ":" + String(dsSecTens) + String(dsSecOnes);
	lcd.setCursor(8, 1);
	lcd.print(time);
	lcd.setCursor(0, 1);
	String MSG = "Mst:";
	if (moisValue > 99)			MSG += String(moisValue);
	else if (moisValue < 100)	MSG += String(moisValue) + "  ";
	else if (moisValue < 10)	MSG += String(moisValue) + "   ";
	lcd.print(MSG);
}

void connectToWifi() {
	Serial.println("Connecting to Wi-Fi...");
	WiFi.begin(ssid, password);
	byte dot = 0;
	while (WiFi.status() != WL_CONNECTED) {
		lcd.setCursor(dot, 1);
		lcd.print(".");
		dot++;
		delay(1000);
	}
	WiFi.setAutoReconnect(true);
	WiFi.persistent(true);
	Serial.print("Connected to ");
	Serial.println(WiFi.SSID());
	lcd.setCursor(0, 0);
	lcd.print("WiFi:" + WiFi.SSID());
}

boolean reconnect() {
    if (client.connect("ESP8266Client")) {
		for  (int i = 0; i < (sizeof(subscribetopic)/sizeof(int)); i++) {
			String pref = prefixtopic;
			pref.concat(subscribetopic[i]);
			client.subscribe((char *) pref.c_str());
			Serial.println(pref);
		}
    }
	return client.connected();
}

void setup() {
	system_update_cpu_freq(160);
	Serial.begin(115200);
	lcd.begin();
	lcd.backlight();
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("Initializing");
	// --- START Setup pin ----
	pinMode(MOISTURE_pin, INPUT);			// Moisture Sensor
	pinMode(DHT_pin, INPUT);				// DHT11
	pinMode(PUMP_pin, OUTPUT);				// PUMP
	pinMode(LIGHT_pin, OUTPUT);				// LIGHT
	dht.setup(DHT_pin);
	// --- END Setup pin ------
	// --- START EEPROM -------
	EEPROM.begin(256);
	EEPROM.get(pumpStateAddr, pumpState);
	EEPROM.get(lightStateAddr, lightState);
	EEPROM.get(pumpOnDelayAddr, pumpOnDelay);
	EEPROM.get(pumpOffDelayAddr, pumpOffDelay);
	EEPROM.get(useLineNotifyAddr, useLineNotify);
	EEPROM.get(lowLimitMoisAddr, lowLimitMois);
	EEPROM.get(lineTokenAddr, lineToken);
	// --- END EEPROM ---------
	digitalWrite(LIGHT_pin, lightState);
	digitalWrite(PUMP_pin, pumpState);
	connectToWifi();
	// --- START ElegantOTA ---
	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Hi! I am ESP8266. ESP-WateringSystem\nVersion: " + String(version));
	});
	AsyncElegantOTA.begin(&server);
    server.begin();
	// --- END ElegantOTA -----
	client.setServer(MQTT_HOST, 1883);
	client.setCallback(callback);
	rtc.halt(false);
	rtc.writeProtect(false);
	LINE.setToken(lineToken);
	delay(1000);
	lcd.clear();
}

void loop() {
	client.loop();
	manualPumpCheck();
	setLight(lightState);
	setPump(pumpState);

	if (millis() - moisReadTime > 1000) {
		moisValue = 1024 - analogRead(MOISTURE_pin);
		Serial.println(moisValue);
		moisReadTime = millis();
	}
	if (moisValue < lowLimitMois*10) {
		if ((millis() - pumpOnStoreTime > (pumpOnDelay * 1000)) && pumpState == 0) {	// Delay pump before turn on
			pumpState = 1;											// Save pump state
			// sendLine("\nNotify - Low moisture\nMoisture : " + String(moistureValue));
		}
		pumpOffStoreTime = millis();								// Reset Pump off store time
	} else {
		if ((millis() - pumpOffStoreTime > (pumpOffDelay * 1000)) && pumpState == 1) {	// Delay pump before turn on
			pumpState = 0;											// Save pump state
		}
		pumpOnStoreTime = millis();									// Reset Pump on store time
	}

	if (millis() - updateTimeDHT > 1000) {
		getDht();
		getTime();
		updateTimeDHT = millis();
	}

	if (millis() - viewPageChangeTime > 3000) {
		if (viewPage > 3) viewPage = 1;
		else viewPage++;
		viewPageChangeTime = millis();
	}

	if (millis() - updateStatus > 500) {
		String PUMP_STATE;
		switch (viewPage) {
		case 1:
			pinDisp();
			lcd.setCursor(0, 0);
			lcd.print("Temp:" + String(temperature));
			lcd.setCursor(8, 0);
			lcd.print("Humi:" + String(humidity));
			break;
		
		case 2:
			pinDisp();
			lcd.setCursor(0, 0);
			if (pumpManualActive == 1) PUMP_STATE = "M    ";
			else PUMP_STATE = convState(pumpState);
			lcd.print("Pu :" + PUMP_STATE);
			lcd.setCursor(8, 0);
			lcd.print("MoL:" + String(lowLimitMois*10));
			break;
		
		case 3:
			pinDisp();
			lcd.setCursor(0, 0);
			lcd.print("Pon:" + delayStr(pumpOnDelay));
			lcd.setCursor(8, 0);
			lcd.print("Pof:" + delayStr(pumpOffDelay));
			break;

		case 4:
			pinDisp();
			lcd.setCursor(0, 0);
			lcd.print("LNE:" + convState(useLineNotify));
			lcd.setCursor(8, 0);
			lcd.print("Li :" + convState(lightState));
			break;
		
		}
		updateStatus = millis();
	}

	if (!client.connected()) {
		if (millis() - mqttReconnTime > 1000) {
			reconnect();
			mqttReconnTime = millis();
		}
	} else {
		if (millis() - mqttInfo > 1000) {
			DynamicJsonDocument docInfo(256);
			docInfo["rssi"]			= String(WiFi.RSSI());
			docInfo["uptime"]		= String(millis()/1000);
			docInfo["dsTime"]		= String(dsHrTens) + String(dsHrOnes) + ":" + String(dsMinTens) + String(dsMinOnes);
			docInfo["pumpState"]	= String(pumpState);
			docInfo["lightState"]	= String(lightState);
			docInfo["temp"]			= String(temperature);
			docInfo["hum"]			= String(humidity);
			docInfo["mois"]			= String(moisValue);
			docInfo["moisLimit"]	= String(lowLimitMois*10);
			String MQTT_STR;
			serializeJson(docInfo, MQTT_STR);
			client.publish("esp/watering/info", MQTT_STR.c_str());
			mqttInfo = millis();
		}
		if (millis() - mqttInfoChart > 300000) { // 5 minutes
			DynamicJsonDocument docInfo(64);
			docInfo["temp"]		= String(temperature);
			docInfo["hum"]		= String(humidity);
			docInfo["mois"]		= String(moisValue);
			String MQTT_STR;
			serializeJson(docInfo, MQTT_STR);
			client.publish("esp/watering/log", MQTT_STR.c_str());
			mqttInfoChart = millis();
		}
		if (firstSend == 0) {
			DynamicJsonDocument docInfo(32);
			docInfo["ipaddr"] = WiFi.localIP().toString();
			String MQTT_STR;
			serializeJson(docInfo, MQTT_STR);
			client.publish("esp/watering/info", MQTT_STR.c_str());
			firstSend = 1;
		}
	}
}
