// Arduino file start here
#define TINY_GSM_MODEM_SIM7600
#define SerialMon Serial
HardwareSerial SerialAT(1);

#if !defined(TINY_GSM_RX_BUFFER)
#define TINY_GSM_RX_BUFFER 650
#endif
#define TINY_GSM_YIELD() { delay(2); }
const char apn[] = "hologram"; // Change this to your Provider details
const char gprsUser[] = "";
const char gprsPass[] = "";
const char server[] = "insert-app-mongo-c57ab58749e5.herokuapp.com"; // Change this to your selection
const char resource[] = "/insertData/0";
const int port = 80;
unsigned long timeout;

#include <TinyGsmClient.h>
TinyGsm modem(SerialAT);
TinyGsmClient client(modem);

void setup()
{
  SerialMon.begin(115200);
  delay(10);
  SerialMon.println("Wait...");
  SerialAT.begin(115200, SERIAL_8N1, 16, 17, false);
  delay(600);
  SerialMon.println("Initializing modem...");
}

void loop()
{
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

  String httpRequestData = "{\"ID\": 55,\"DT\": \"2023-15-11 20:00:00\",\"D\": \"GPS123\",\"Lng\": 17.777777,\"Lat\": 45.777777}";

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
  delay(5000);
}
// End of Arduino file
