#define TINY_GSM_MODEM_SIM7600
// Set serial for debug console (to Serial Monitor, default speed 115200)
#define SerialMon Serial
// Set serial for AT commands
HardwareSerial SerialAT(1);

#if !defined(TINY_GSM_RX_BUFFER)
#define TINY_GSM_RX_BUFFER 1024 // 650
#endif

#include <TinyGsmClient.h>

const char apn[] = "hologram"; // Change this to your Provider details
const char gprsUser[] = "";
const char gprsPass[] = "";
const char server[] = "insert-app-mongo-c57ab58749e5.herokuapp.com"; // Change this to your selection
const char resource[] = "/insertData/0";
const int port = 80;
unsigned long timeout;

TinyGsm modem(SerialAT);
TinyGsmClient client(modem);

float lat = 0;
float lon = 0;

void setup()
{
  SerialMon.begin(115200);
  delay(10);
  SerialMon.println("Place your board outside to catch satellite signal");

  // Set module baud rate and UART pins for GPS
  SerialAT.begin(115200, SERIAL_8N1, 16, 17, false);
  delay(600);
  SerialMon.println("Initializing modem...");
  if (!modem.restart())
  {
    Serial.println("Failed to restart modem, attempting to continue without restarting");
  }

  // Print modem info
  String modemName = modem.getModemName();
  delay(500);
  SerialMon.println("Modem Name: " + modemName);

  String modemInfo = modem.getModemInfo();
  delay(500);
  SerialMon.println("Modem Info: " + modemInfo);
}

void loop()
{
  // GPS Section
  modem.enableGPS();

  for (int i = 0; i < 3; i++)
  {
    float speed = 0;
    float alt = 0;
    int vsat = 0;
    int usat = 0;
    float accuracy = 0;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int min = 0;
    int sec = 0;

    SerialMon.println("Requesting current GPS/GNSS/GLONASS location");
    if (modem.getGPS(&lat, &lon, &speed, &alt, &vsat, &usat, &accuracy,
                     &year, &month, &day, &hour, &min, &sec))
    {
      SerialMon.println("Latitude: " + String(lat, 8) + "\tLongitude: " + String(lon, 8));
      SerialMon.println("Speed: " + String(speed) + "\tAltitude: " + String(alt));
      SerialMon.println("Visible Satellites: " + String(vsat) + "\tUsed Satellites: " + String(usat));
      SerialMon.println("Accuracy: " + String(accuracy));
      SerialMon.println("Year: " + String(year) + "\tMonth: " + String(month) + "\tDay: " + String(day));
      SerialMon.println("Hour: " + String(hour) + "\tMinute: " + String(min) + "\tSecond: " + String(sec));

      // Use the retrieved GPS coordinates in the HTTP request
      String httpRequestData = "{\"ID\": 55,\"DT\": \"2023-15-11 20:00:00\",\"D\": \"GPS123\",\"Lng\": " + String(lon, 8) + ",\"Lat\":" + String(lat, 8) + "}";

      // HTTP POST Section
      modem.restart();
      SerialMon.print("Waiting for network...");
      if (!modem.waitForNetwork())
      {
        SerialMon.println(" fail");
        delay(1000);
        return;
      }

      SerialMon.println(" success");
      if (modem.isNetworkConnected())
      {
        SerialMon.println("Network connected");
      }

      SerialMon.print(F("Connecting to "));
      SerialMon.print(apn);
      if (!modem.gprsConnect(apn, gprsUser, gprsPass))
      {
        SerialMon.println(" fail");
        delay(1000);
        return;
      }
      SerialMon.println(" success");

      if (modem.isGprsConnected())
      {
        SerialMon.println("GPRS connected");
      }

      if (!client.connect(server, port))
      {
        SerialMon.println(" fail");
        return;
      }

      SerialMon.println("Performing HTTP POST request...");

      client.print(String("POST ") + resource + " HTTP/1.1\r\n");
      client.print(String("Host: ") + server + "\r\n");
      client.println("Connection: close");
      client.println("Content-Type: application/json");
      client.print("Content-Length: ");
      client.println(httpRequestData.length());
      client.println();
      client.println(httpRequestData);

      // Print the response from the server
      while (client.connected() && client.available())
      {
        char c = client.read();
        SerialMon.print(c);
      }

      SerialMon.println();
      client.stop();
      SerialMon.println(F("Server disconnected"));
      modem.gprsDisconnect();
      SerialMon.println(F("GPRS disconnected"));

      // Wait before the next iteration
      delay(20000); // 20 seconds delay
      // End of HTTP POST Section

      break; // Break out of the loop if GPS data is successfully retrieved
    }
    
    delay(15000L); // Wait for 15 seconds before the next attempt
  }

  SerialMon.println("Retrieving GPS/GNSS/GLONASS location again as a string");
  String gps_raw = modem.getGPSraw();
  SerialMon.println("GPS/GNSS Based Location String: " + gps_raw);
  SerialMon.println("Disabling GPS");
  delay(200);

  // ... (continue with the rest of the loop)
}
