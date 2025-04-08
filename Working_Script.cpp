// Define modem model
#define TINY_GSM_MODEM_SIM7600

// Include necessary libraries
#include <TinyGsmClient.h>
#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_SHT31.h"
#include <time.h> // Include the time library

Adafruit_SHT31 sht31 = Adafruit_SHT31();

// Define serial interfaces and pins
#define SerialMon Serial
#define SerialAT Serial1
#define PIN_TX 27
#define PIN_RX 26
#define UART_BAUD 115200
#define POWER_PIN 25
#define PWR_PIN 4
#define LED_PIN 12

// GPRS credentials and server details
const char apn[] = "hologram";
const char gprsUser[] = "";
const char gprsPass[] = "";
const char server[] = "insert-app-mongo-c57ab58749e5.herokuapp.com";
const char resource[] = "/insertData/0";
const int port = 80;

// Modem instance
TinyGsm modem(SerialAT);
TinyGsmClient client(modem);

void setup() {
    // Initialize serial monitor
    SerialMon.begin(115200);
    delay(10);

    // Check SHT31 sensor
    if (!sht31.begin(0x44)) {   
        Serial.println("Check circuit. SHT31 not found!");
        while (1) delay(1);
    }

    // Initialize LED, power, and PWR pins
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    pinMode(POWER_PIN, OUTPUT);
    digitalWrite(POWER_PIN, HIGH);
    pinMode(PWR_PIN, OUTPUT);
    digitalWrite(PWR_PIN, HIGH);
    delay(500); // Wait before pulling PWR_PIN low
    digitalWrite(PWR_PIN, LOW);
    delay(3000); // Hold PWR_PIN low for 3 seconds to ensure modem turns on

    // Initialize serial communication with modem
    SerialAT.begin(UART_BAUD, SERIAL_8N1, PIN_RX, PIN_TX);
    delay(3000); // Wait for modem to initialize

    SerialMon.println("Initializing modem...");
    if (!modem.init()) {
        SerialMon.println("Failed to restart modem, attempting to continue without restarting");
    } else {
        SerialMon.println("Modem initialized successfully.");
    }

    // Configure NTP servers
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    SerialMon.println("Waiting for NTP time sync...");
    while (!time(nullptr)) {
        delay(1000);
        SerialMon.print(".");
    }
    SerialMon.println("\nTime synchronized.");
}

void loop() {
    float lat, lon;
    float temp = sht31.readTemperature();
    float hum = sht31.readHumidity();

    // Enable GPS and wait for signal acquisition
    SerialMon.println("Enabling GPS...");
    modem.enableGPS();
    delay(60000); // Wait 60 seconds for GPS signal acquisition

    // Attempt to get GPS location
    SerialMon.println("Attempting to get GPS location...");
    if (modem.getGPS(&lat, &lon)) {
        SerialMon.printf("GPS Location: Latitude: %f, Longitude: %f\n", lat, lon);
        // Proceed with network check and HTTP POST request
        performHttpPost(lat, lon, temp, hum);
    } else {
        SerialMon.println("Failed to get GPS location.");
    }

    // Disable GPS to save power
    modem.disableGPS();
    delay(10000); // Wait for 10 seconds before next loop iteration
}

void performHttpPost(float lat, float lon, float temp, float hum) {
    // Get the current time
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);

    // Format the timestamp as YYYY-DD-MM HH:MM:SS
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%d-%m %H:%M:%S", timeinfo);

    // Check network connection and connect to GPRS
    if (!checkNetworkAndConnect()) {
        return; // Exit if network connection or GPRS connection fails
    }

    // Perform HTTP POST request
    if (client.connect(server, port)) {
        SerialMon.println("Connected to server, performing HTTP POST request...");
        String httpRequestData = "{\"trackerID\": 55,\"DT\": \"" + String(timestamp) + "\",\"D\": \"GPS123\",\"Temp\": " + String(temp, 5) + ",\"Hum\": " + String(hum, 5) + ",\"Lng\": " + String(lon, 8) + ",\"Lat\":" + String(lat, 8) + "}";
        client.print(String("POST ") + resource + " HTTP/1.1\r\n");
        client.print(String("Host: ") + server + "\r\n");
        client.println("Connection: close");
        client.println("Content-Type: application/json");
        client.print("Content-Length: ");
        client.println(httpRequestData.length());
        client.println();
        client.println(httpRequestData);

        // Read and print the response from the server
        unsigned long timeout = millis();
        while (client.connected() && millis() - timeout < 10000L) {
            if (client.available()) {
                char c = client.read();
                SerialMon.print(c);
                timeout = millis();
            }
        }
        SerialMon.println("\nHTTP POST request complete.");
        client.stop();
    } else {
        SerialMon.println("Connection to server failed.");
    }

    // Disconnect GPRS
    modem.gprsDisconnect();
    SerialMon.println("GPRS disconnected.");
}

bool checkNetworkAndConnect() {
    SerialMon.print("Checking network status...");
    if (!modem.isNetworkConnected()) {
        SerialMon.println(" disconnected, attempting to reconnect...");
        if (!modem.waitForNetwork()) {
            SerialMon.println("Network connection failed, retrying in 10 seconds...");
            delay(10000);
            return false;
        }
        SerialMon.println("Network reconnected.");
    } else {
        SerialMon.println("Network is connected.");
    }

    SerialMon.print("Connecting to GPRS APN: ");
    SerialMon.println(apn);
    if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
        SerialMon.println("GPRS connection failed, retrying in 10 seconds...");
        delay(10000);
        return false;
    }
    SerialMon.println("GPRS connected.");
    return true;
}
