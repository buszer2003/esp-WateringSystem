/*
== ESP - Plant Watering System ==
A0 -- Moisture Sensor
D0 -- DHT11
D1 -- SCL		(LCD)
D2 -- SDA		(LCD)
D4 -- RST		(DS1302)
D5 -- DAT		(DS1302)
D6 -- CLK		(DS1302)
D3 -- Relay 1	(Light)
D7 -- Relay 2	(Pump)
*/

const char version[6] = "0.7.1";

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
const byte PUMP_pin				= D7;
const byte LIGHT_pin			= D3;
DS1302 rtc(D4, D5, D6);

// #define MQTT_HOST IPAddress(192, 168, 1, 3)
#define MQTT_HOST "bzcom.ddns.net"
// #define MQTT_HOST "broker.hivemq.com"

Time t;

LiquidCrystal_I2C lcd(0x27, 16, 2);

char ssid[33];
char password[65];

// const char *ssid     = "BZ_IOT";
// const char *password = "Password";
// const char *ssid     = "R&D_2";
// const char *password = "1qaz7ujmrd2";

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
char lineToken[44];
byte viewPage = 1;
unsigned int moisValue = 0;
byte lowLimitMois = 30;

// EEPROM Address
byte lightStateAddr = 1;
byte useLineNotifyAddr = 2;
byte pumpOnDelayAddr = 3;
byte pumpOffDelayAddr = 4;
byte lowLimitMoisAddr = 5;
byte lineTokenAddr = 8;
byte ssidAddr = 16;
byte passAddr = 50;

unsigned long mqttInfo;
unsigned long mqttInfoChart;
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

void callback(char* topic, byte* message, unsigned int length) {
    String messageTemp;

    for (int i = 0; i < length; i++) {
        Serial.print((char)message[i]);
        messageTemp += (char)message[i];
    }
	Serial.println();

    DynamicJsonDocument doc(256);
    deserializeJson(doc, messageTemp);
	Serial.println(messageTemp);

    if (strcmp(topic, "watering/pump/set") == 0) {
		bool flagPumpSetting = doc["flag"];
		if (flagPumpSetting == 1) {
			byte getPumpOnDelay = doc["on"];
			byte getPumpOffDelay = doc["off"];
			pumpOnDelay = getPumpOnDelay;
			pumpOffDelay = getPumpOffDelay;
			EEPROM.put(pumpOnDelayAddr, pumpOnDelay);
			EEPROM.put(pumpOffDelayAddr, pumpOffDelay);
			EEPROM.commit();
			DynamicJsonDocument docSend(128);
			String MQTT_STR;
			String MSG = "\nPump on/off delay (" + String(pumpOnDelay) + "s/" + String(pumpOffDelay) + "s)";
			docSend["status"] = "OK";
			docSend["message"] = "Pump on/off delay (" + String(pumpOnDelay) + "s/" + String(pumpOffDelay) + "s)";
			serializeJson(docSend, MQTT_STR);
			client.publish("watering/savestatus", MQTT_STR.c_str());
			sendLine(MSG);
		}
	}
	if (strcmp(topic, "watering/light") == 0) {
		bool flagLight = doc["flag"];
		if (flagLight == 1) {
			bool getLight = doc["state"];
			if (getLight != lightState) {
				lightState = getLight;
				EEPROM.put(lightStateAddr, lightState);
				EEPROM.commit();
				setLight(lightState);
				DynamicJsonDocument docSend(128);
				String MQTT_STR;
				String lightStateStr;
				if (lightState == 0)		lightStateStr = "OFF";
				else if (lightState == 1)	lightStateStr = "ON";
				String MSG = "\nLight " + lightStateStr;
				docSend["status"] = "OK";
				docSend["message"] = "Light state = " + lightStateStr;
				serializeJson(docSend, MQTT_STR);
				client.publish("watering/savestatus", MQTT_STR.c_str());
				sendLine(MSG);
			}
		}
	}
	if (strcmp(topic, "watering/line/act") == 0) {
		bool flagUseLineNotify = doc["flag"];
		if (flagUseLineNotify == 1) {
			bool getUseLineNotify = doc["state"];
			if (getUseLineNotify != useLineNotify) {
				useLineNotify = getUseLineNotify;
				EEPROM.put(useLineNotifyAddr, useLineNotify);
				EEPROM.commit();
				String MQTT_STR;
				DynamicJsonDocument docSend(128);
				String useLineNotifyStr;
				if (useLineNotify == 0)			useLineNotifyStr = "Not Use";
				else if (useLineNotify == 1)	useLineNotifyStr = "Use";
				String MSG = "\nLINE Notify\n" + useLineNotifyStr;
				docSend["status"] = "OK";
				docSend["message"] = "Line Notify setting = " + useLineNotifyStr;
				serializeJson(docSend, MQTT_STR);
				client.publish("watering/savestatus", MQTT_STR.c_str());
				LINE.notify(MSG);
			}
		}
	}
	if (strcmp(topic, "watering/line/token") == 0) {
		bool flagLineToken = doc["flag"];
		if (flagLineToken == 1) {
			String getLineToken = doc["token"];
			Serial.println(getLineToken);
			getLineToken.toCharArray(lineToken, 44);
			DynamicJsonDocument docSend(128);
			String MQTT_STR;
			if (strlen(lineToken) == 43) {
				EEPROM.put(lineTokenAddr, lineToken);
				EEPROM.commit();
				LINE.notify("\nLINE Notify token changed!");
				LINE.setToken(lineToken);
				docSend["status"] = "OK";
				docSend["message"] = "Saved Line Token";
				docSend["lineToken"] = String(lineToken);
			} else {
				docSend["status"] = "ERROR";
				docSend["message"] = "Invalid Line Token";
			}
			serializeJson(docSend, MQTT_STR);
			client.publish("watering/savestatus", MQTT_STR.c_str());
		}
	}
	if (strcmp(topic, "watering/mois") == 0) {
		bool flagLowLimitMois = doc["flag"];
		if (flagLowLimitMois == 1) {
			byte getLowLimitMois = doc["value"];
			lowLimitMois = getLowLimitMois;
			EEPROM.put(lowLimitMoisAddr, lowLimitMois);
			EEPROM.commit();
			DynamicJsonDocument docSend(128);
			String MQTT_STR;
			String MSG = "\nSet moisture low limit = " + String(lowLimitMois*10);
			docSend["status"] = "OK";
			docSend["message"] = "Set moisture low limit = " + String(lowLimitMois*10);
			serializeJson(docSend, MQTT_STR);
			client.publish("watering/savestatus", MQTT_STR.c_str());
			sendLine(MSG);
		}
	}
	if (strcmp(topic, "watering/time") == 0) {
		bool flagUpdateTime = doc["flag"];
    	if (flagUpdateTime == 1) {
			byte Second	= doc["ss"];
			byte Minute	= doc["mm"];
			byte Hour	= doc["HH"];
			byte Day	= doc["dd"];
			byte Month	= doc["MM"];
			int Year	= doc["yyyy"];
			if (Hour != dsHr || Minute != dsMin) {
				rtc.setTime(Hour, Minute, Second);
				rtc.setDate(Day, Month, Year);
			}
			DynamicJsonDocument docSend(128);
    	    String MQTT_STR;
			docSend["status"] = "OK";
			docSend["message"] = "updated";
			serializeJson(docSend, MQTT_STR);
    	    client.publish("watering/savestatus", MQTT_STR.c_str());    
		}
	}
	if (strcmp(topic, "watering/get/token") == 0) {
		bool flagGetToken = doc["flag"];
		if (flagGetToken == 1) {
			char getTokenTemp[44] = "";
			EEPROM.get(lineTokenAddr, getTokenTemp);
			DynamicJsonDocument docSend(128);
			String MQTT_STR;
			docSend["status"]	= "OK";
			docSend["message"]	= String(getTokenTemp);
			serializeJson(docSend, MQTT_STR);
			client.publish("watering/savestatus", MQTT_STR.c_str());
		}
	}
	if (strcmp(topic, "watering/backlight") == 0) {
		bool flagBacklight = doc["flag"];
		if (flagBacklight == 1) {
			bool getBacklight = doc["state"];
			String stateStr = "";
			if (getBacklight == 0) lcd.noBacklight(), stateStr = "OFF";
			else if (getBacklight == 1) lcd.backlight(), stateStr = "ON";
			DynamicJsonDocument docSend(128);
			String MQTT_STR;
			docSend["status"]	= "OK";
			docSend["message"]	= "Backlight (" + String(stateStr) + ")";
			serializeJson(docSend, MQTT_STR);
			client.publish("watering/savestatus", MQTT_STR.c_str());
		}
	}
}

String convState(int state) {
	if (state == 0) return "OFF ";
	else if (state == 1) return "ON  ";
	else return "E   ";
}

String dispStr(int num) {
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
    lcd.setCursor(0, 0);
    lcd.print(ssid);
	WiFi.begin(ssid, password);
	byte retry = 0, config_done = 0, dot = 0;
	WiFi.mode(WIFI_STA);
    while (WiFi.status() != WL_CONNECTED) {
		delay(500);
        Serial.print(".");
        if (dot > 15) dot = 0, lcd.setCursor(0, 1), lcd.print("                ");
        lcd.setCursor(dot, 1);
		lcd.print(">");
		dot++;
		if (retry++ > 19) {
			Serial.println("Connection timeout expired! Start SmartConfig...");
            dot = 0;
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("SmartConfig...");
			WiFi.beginSmartConfig();
			while (true) {
				delay(500);
				Serial.print(".");
                if (dot > 15) dot = 0, lcd.setCursor(0, 1), lcd.print("                ");
                lcd.setCursor(dot, 1);
		        lcd.print(">");
		        dot++;
				if (WiFi.smartConfigDone()) {
					Serial.println("\nSmartConfig successfully configured");
                    lcd.setCursor(0, 1);
                    lcd.print("                ");
                    lcd.print("done");
					config_done = 1;
					break;
				}
			}
			if (config_done == 1) break;
		}
	}
    if (config_done) {
        String TEXT_SSID = WiFi.printSSID(Serial);
	    String TEXT_PASS = WiFi.printPASS(Serial);
	    TEXT_SSID.toCharArray(ssid, 33);
	    TEXT_PASS.toCharArray(password, 65);
	    EEPROM.put(ssidAddr, ssid);
	    EEPROM.put(passAddr, password);
	    EEPROM.commit();
    }
	WiFi.setAutoReconnect(true);
	WiFi.persistent(true);
	Serial.print("Connected to ");
	Serial.println(WiFi.SSID());
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("WiFi:" + WiFi.SSID());
	lcd.setCursor(0, 1);
	lcd.print(WiFi.localIP().toString());
}

void reconnect() {
    if (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (client.connect("ESP8266Client")) {
            Serial.println("connected");
            client.subscribe("watering/pump/set");
            client.subscribe("watering/light");
            client.subscribe("watering/line/act");
            client.subscribe("watering/line/token");
            client.subscribe("watering/mois");
            client.subscribe("watering/time");
			client.subscribe("watering/backlight");
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
        }
    }
}

void setup() {
	system_update_cpu_freq(160);
	digitalWrite(PUMP_pin, LOW);
	digitalWrite(LIGHT_pin, LOW);
	Serial.begin(115200);
	lcd.begin();
	lcd.backlight();
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("V" + String(version));
    delay(1000);
	// --- START Setup pin ----
	pinMode(MOISTURE_pin, INPUT);			// Moisture Sensor
	pinMode(DHT_pin, INPUT);				// DHT11
	pinMode(PUMP_pin, OUTPUT);				// PUMP
	pinMode(LIGHT_pin, OUTPUT);				// LIGHT
	dht.setup(DHT_pin);
	// --- END Setup pin ------
	// --- START EEPROM -------
	EEPROM.begin(256);
	EEPROM.get(lightStateAddr, lightState);
	EEPROM.get(pumpOnDelayAddr, pumpOnDelay);
	EEPROM.get(pumpOffDelayAddr, pumpOffDelay);
	EEPROM.get(useLineNotifyAddr, useLineNotify);
	EEPROM.get(lowLimitMoisAddr, lowLimitMois);
	EEPROM.get(lineTokenAddr, lineToken);
    EEPROM.get(ssidAddr, ssid);
    EEPROM.get(passAddr, password);
	// --- END EEPROM ---------
	connectToWifi();
	// --- START ElegantOTA ---
	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Hi! I am ESP8266. ESP-WateringSystem\nVersion: " + String(version));
	});
	AsyncElegantOTA.begin(&server, "admin", "admin");
    server.begin();
	// --- END ElegantOTA -----
	client.setServer(MQTT_HOST, 1883);
	client.setCallback(callback);
	rtc.halt(false);
	rtc.writeProtect(false);
	LINE.setToken(lineToken);
	delay(2000);
	lcd.clear();
}

void loop() {
	client.loop();
	setLight(lightState);
	setPump(pumpState);

	if (millis() - moisReadTime > 1000) {
		moisValue = 1024 - analogRead(MOISTURE_pin);
		// Serial.println(moisValue);
		moisReadTime = millis();
	}
	if (moisValue < lowLimitMois*10) {
		if ((millis() - pumpOnStoreTime > pumpOnDelay * 1000) && pumpState == 0) {	// Delay pump before turn on
			pumpState = 1;											// Save pump state
			sendLine("\nLow moisture (" + String(moisValue) + ")");
		}
		pumpOffStoreTime = millis();								// Reset Pump off store time
	} else {
		if ((millis() - pumpOffStoreTime > pumpOffDelay * 1000) && pumpState == 1) {	// Delay pump before turn on
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
			lcd.print("MoL:" + dispStr(lowLimitMois*10));
			break;
		
		case 3:
			pinDisp();
			lcd.setCursor(0, 0);
			lcd.print("LNE:" + convState(useLineNotify));
			lcd.setCursor(8, 0);
			lcd.print("Li :" + convState(lightState));
			break;

		case 4:
			pinDisp();
			lcd.setCursor(0, 0);
			lcd.print("Pon:" + dispStr(pumpOnDelay));
			lcd.setCursor(8, 0);
			lcd.print("Pof:" + dispStr(pumpOffDelay));
			break;
		
		}
		updateStatus = millis();
	}

	if (!client.connected()) {
		reconnect();
	} else {
		if (millis() - mqttInfo > 1000) {
			DynamicJsonDocument docInfo(256);
			docInfo["rssi"]			= String(WiFi.RSSI());
			docInfo["ipaddr"]		= WiFi.localIP().toString();
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
			client.publish("watering/info", MQTT_STR.c_str());
			mqttInfo = millis();
		}
		if (millis() - mqttInfoChart > 300000) { // 5 minutes
			DynamicJsonDocument docInfo(64);
			docInfo["temp"]		= String(temperature);
			docInfo["hum"]		= String(humidity);
			docInfo["mois"]		= String(moisValue);
			String MQTT_STR;
			serializeJson(docInfo, MQTT_STR);
			client.publish("watering/log", MQTT_STR.c_str());
			mqttInfoChart = millis();
		}
	}
}
