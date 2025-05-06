// Define modem model
#define TINY_GSM_MODEM_SIM7600

// Include necessary libraries
#include <TinyGsmClient.h>
#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_SHT31.h"
#include <time.h> // Include the time library
#include <vector>
#include <ArduinoJson.h> // Include ArduinoJson for JSON handling

Adafruit_SHT31 sht31 = Adafruit_SHT31(); // Corrected type name

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

// Buffer to store GPS, temperature, and humidity readings
std::vector<String> dataBuffer;

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

    // Disable command echo
    modem.sendAT("ATE0");
    if (modem.waitResponse(1000) != 1) {
        SerialMon.println("Failed to disable command echo.");
    }

    // Ensure GPS is powered on with retries
    bool gpsPoweredOn = false;
    for (int i = 0; i < 5; i++) { // Retry up to 5 times
        modem.sendAT("+CGNSPWR=1");
        if (modem.waitResponse(2000) == 1) {
            gpsPoweredOn = true;
            break;
        }
        SerialMon.println("Failed to power on GPS. Retrying...");
        delay(2000); // Wait 2 seconds before retrying
    }

    if (!gpsPoweredOn) {
        SerialMon.println("Failed to power on GPS after multiple attempts.");
    } else {
        SerialMon.println("GPS powered on successfully.");
    }

    // Verify GPS power status
    modem.sendAT("+CGNSPWR?");
    if (modem.waitResponse(1000) == 1) {
        String gpsPowerStatus = modem.stream.readStringUntil('\n');
        SerialMon.println("GPS Power Status: " + gpsPowerStatus);
    } else {
        SerialMon.println("Failed to retrieve GPS power status.");
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
    static bool gpsEnabled = false; // Track GPS state
    static bool gprsConnected = false; // Track GPRS connection state
    static unsigned long lastCaptureTime = 0; // Track the last capture time
    static unsigned long lastPostTime = 0; // Track the last HTTP POST time

    if (!gpsEnabled) {
        SerialMon.println("Enabling GPS...");
        modem.enableGPS();
        delay(15000); // Wait for GPS to enable
        gpsEnabled = true; // Mark GPS as enabled
    }

    float lat, lon;
    float speed = 0;
    float alt = 0;
    int vsat = 0, usat = 0; // Added vsat and usat variables
    float accuracy = 0;
    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;

    float temp = sht31.readTemperature();
    float hum = sht31.readHumidity();

    // Capture data every 5 seconds
    if (millis() - lastCaptureTime >= 5000) {
        lastCaptureTime = millis();

        // Attempt to get GPS location
        if (modem.getGPS(&lat, &lon, &speed, &alt, &vsat, &usat, &accuracy, &year, &month, &day, &hour, &min, &sec)) {
            // Get the battery level
            int batteryLevel = getBatteryLevel();
            SerialMon.printf("Battery Level: %d%%\n", batteryLevel);

            // Format data as JSON and add to buffer
            StaticJsonDocument<256> doc;
            doc["Lat"] = lat;
            doc["Lng"] = lon;
            doc["Temp"] = temp;
            doc["Hum"] = hum;
            doc["Speed"] = speed;
            doc["DT"] = String(year) + "-" + String(month) + "-" + String(day) + " " + String(hour) + ":" + String(min) + ":" + String(sec);
            doc["Batt"] = batteryLevel; // Add battery level to the object

            String jsonData;
            serializeJson(doc, jsonData);
            dataBuffer.push_back(jsonData);

            SerialMon.println("Data captured and added to buffer.");
        } else {
            SerialMon.println("Failed to get GPS location.");
        }
    }

    // Perform HTTP POST request every 30 seconds
    if (millis() - lastPostTime >= 30000) {
        lastPostTime = millis();

        if (!gprsConnected) {
            SerialMon.println("Connecting to GPRS...");
            if (!checkNetworkAndConnect()) {
                SerialMon.println("Failed to connect to GPRS.");
                return; // Exit if GPRS connection fails
            }
            gprsConnected = true; // Mark GPRS as connected
        }

        if (!dataBuffer.empty()) {
            performHttpPost(dataBuffer);
            dataBuffer.clear(); // Clear the buffer after sending
        } else {
            SerialMon.println("Buffer is empty, skipping HTTP POST.");
        }
    }
}

int getBatteryLevel() {
    SerialMon.println("Retrieving battery level...");

    // Disable command echo to avoid receiving the echoed command
    modem.sendAT("E0");
    modem.waitResponse(1000); // Wait for the modem to process the command

    // Send the AT+CBC command to get battery status
    modem.sendAT("+CBC");
    if (modem.waitResponse(1000, "+CBC:") == 1) { // Wait for the response containing "+CBC:"
        String response = modem.stream.readStringUntil('\n'); // Read the response line
        SerialMon.println("Response: " + response); // Print the response for debugging

        // Extract the voltage value from the response
        int voltageStart = response.indexOf(" ") + 1; // Find the first space
        int voltageEnd = response.indexOf("V", voltageStart); // Find the "V" character
        if (voltageStart > 0 && voltageEnd > voltageStart) {
            String voltageStr = response.substring(voltageStart, voltageEnd); // Extract the voltage string
            voltageStr.trim(); // Remove any extra whitespace
            float voltage = voltageStr.toFloat(); // Convert the voltage to a float
            SerialMon.printf("Battery Voltage: %.3fV\n", voltage);

            // Map the voltage to a percentage (adjust these values based on your battery's specifications)
            if (voltage >= 4.2) return 100; // Fully charged
            if (voltage <= 3.0) return 0;   // Fully discharged
            return (int)((voltage - 3.0) / (4.2 - 3.0) * 100); // Map voltage to percentage
        }
    }

    SerialMon.println("Failed to retrieve battery level.");
    return -1; // Return -1 if the battery level could not be retrieved
}

void flashLED(int pin, int times, int delayMs) {
    for (int i = 0; i < times; i++) {
        digitalWrite(pin, HIGH);
        delay(delayMs);
        digitalWrite(pin, LOW);
        delay(delayMs);
    }
}

void performHttpPost(const std::vector<String>& buffer) {
    // Ensure GPRS connection is active
    if (!modem.isGprsConnected()) {
        SerialMon.println("GPRS is not connected. Attempting to reconnect...");
        if (!checkNetworkAndConnect()) {
            SerialMon.println("Failed to reconnect to GPRS.");
            return;
        }
    }

    // Attempt to reuse the connection for multiple HTTP POST requests
    if (!client.connected()) {
        SerialMon.println("Establishing a new connection to the server...");
        if (!client.connect(server, port)) {
            SerialMon.println("Failed to connect to the server.");
            return;
        }
    }

    // Prepare JSON object with buffer data as a string
    SerialMon.println("Performing HTTP POST request...");
    String bufferData = "[";
    for (size_t i = 0; i < buffer.size(); i++) {
        bufferData += buffer[i];
        if (i < buffer.size() - 1) {
            bufferData += ",";
        }
    }
    bufferData += "]";

    // Get the battery level
    int batteryLevel = getBatteryLevel();
    SerialMon.printf("Battery Level: %d%%\n", batteryLevel);

    // Construct the final JSON object
    String httpRequestData = "{\"trackerID\": 777,\"D\": \"GPS123\",\"data\": " + bufferData + "}";

    client.print(String("POST ") + resource + " HTTP/1.1\r\n");
    client.print(String("Host: ") + server + "\r\n");
    client.println("Connection: keep-alive"); // Use persistent connection
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(httpRequestData.length());
    client.println();
    client.println(httpRequestData);

    // Read and print the response from the server
    unsigned long timeout = millis();
    bool success = false;
    while (client.connected() && millis() - timeout < 10000L) {
        if (client.available()) {
            char c = client.read();
            SerialMon.print(c);
            if (c == '2') { // Check for HTTP 2xx success response
                success = true;
            }
            timeout = millis();
        }
    }
    SerialMon.println("\nHTTP POST request complete.");

    if (success) {
        SerialMon.println("Data successfully inserted into MongoDB. Flashing LED...");
        flashLED(LED_PIN, 10, 100); // Flash LED 5 times quickly
    } else {
        SerialMon.println("Server response did not indicate success.");
    }
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