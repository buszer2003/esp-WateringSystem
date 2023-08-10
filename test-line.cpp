#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <TridentTD_LineNotify.h>

const char *ssid     = "BZ_IOT";
const char *password = "Password";

#define lineToken "JvpfgpVVnFgG4zqoM1gaxCJfhxyjvL3Wzvax1Rq2f4L"

unsigned int count = 1;

IPAddress local_IP(192, 168, 1, 221);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 1, 1);
IPAddress secondaryDNS(192, 168, 1, 3);

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

void setup() {
	Serial.begin(115200);
	connectToWifi();
	LINE.setToken(lineToken);
}

void loop() {
	LINE.notify("Test send from ESP8266 " + String(count));
	count++;
	delay(5000);
}
