/**
 * This sketch connects an Sparkfun Things Plus ESP 32
 * with a SparkFun Environmental Combo Breakout - CCS811/BME280 (Qwiic)
 * to a WiFi network, and runs a
 * tiny HTTP server to serve air quality metrics to Prometheus.
 */
 
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <SparkFunBME280.h>
#include <SparkFunCCS811.h>
#define CCS811_ADDR 0x5B //Default I2C Address

// Config ----------------------------------------------------------------------
const int port = 9926;
WebServer server(port);

// Optional.
const char* deviceId = "ESP32_1";

const char* ssid = "YOURSSID";
const char* password = "YOURPASSWORD";


// Uncomment the line below to configure a static IP address.
// #define staticip
#ifdef staticip
IPAddress static_ip(192, 168, 0, 0);
IPAddress gateway(192, 168, 0, 0);
IPAddress subnet(255, 255, 255, 0);
#endif

// Hardware options for AirGradient DIY sensor.
const bool hastemp = true;
const bool hashumidity = true;
const bool haspressure = true;
const bool hasco2 = true;
const bool hastvoc = true;

// The frequency of measurement updates.
const int updateFrequency = 5000;
  
// For housekeeping.
long lastUpdate;
int counter = 0;

//Global sensor objects
CCS811 myCCS811(CCS811_ADDR);

#define SEALEVELPRESSURE_HPA (1013.25)

BME280 bme; // I2C

// LED Pin
const int ledPin = 13;

// Set web server port number to 80
//WiFiServer server(port);

// Config End ------------------------------------------------------------------


void setup() {
  Serial.begin(115200);

   Wire.begin(); //Inialize I2C Harware

        //It is recommended to check return status on .begin(), but it is not
        //required.
        CCS811Core::CCS811_Status_e returnCode = myCCS811.beginWithStatus();
        Serial.print("CCS811 begin exited with: ");
        Serial.println(myCCS811.statusString(returnCode));

//Initialize BME280
        //For I2C, enable the following and disable the SPI section
        bme.settings.commInterface = I2C_MODE;
        bme.settings.I2CAddress = 0x77;
        bme.settings.runMode = 3; //Normal mode
        bme.settings.tStandby = 0;
        bme.settings.filter = 4;
        bme.settings.tempOverSample = 5;
        bme.settings.pressOverSample = 5;
        bme.settings.humidOverSample = 5;

        delay(10); //Make sure sensor had enough time to turn on. BME280 requires 2ms to start up.
        byte id = bme.begin(); //Returns ID of 0x60 if successful
        if (id != 0x60)
        {
                Serial.println("Problem with BME280");
                while (1);
        }
        pinMode(ledPin, OUTPUT);

  // Set static IP address if configured.
  #ifdef staticip
  WiFi.config(static_ip,gateway,subnet);
  #endif

  // Set WiFi mode to client (without this it may try to act as an AP).
  WiFi.mode(WIFI_STA);
  
  // Configure Hostname
  if ((deviceId != NULL) && (deviceId[0] == '\0')) {
    Serial.printf("No Device ID is Defined, Defaulting to board defaults");
  }
  else {
   // wifi_station_set_hostname(deviceId);
   // WiFi.setHostname(deviceId);
  }
  
  // Setup and wait for WiFi.
  WiFi.begin(ssid, password);
  Serial.println("");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Hostname: ");
  //Serial.println(WiFi.hostname());
  server.on("/", HandleRoot);
  server.on("/metrics", HandleRoot);
  server.onNotFound(HandleNotFound);

  server.begin();
  Serial.println("HTTP server started at ip " + WiFi.localIP().toString() + ":" + String(port));
  //showTextRectangle("Listening To", WiFi.localIP().toString() + ":" + String(port),true);
}

void loop() {

    if (myCCS811.dataAvailable())
        {
                //Calling this function updates the global tVOC and eCO2 variables
                myCCS811.readAlgorithmResults();

                float BMEtempC = bme.readTempC();
                float BMEhumid = bme.readFloatHumidity();

                //This sends the temperature data to the CCS811
                myCCS811.setEnvironmentalData(BMEhumid, BMEtempC);
        }
        else if (myCCS811.checkForStatusError())
        {
                //If the CCS811 found an internal error, print it.
                printSensorError();
        }
        
  long t = millis();

  server.handleClient();
}

String GenerateMetrics() {
  String message = "";
  String idString = "{id=\"" + String(deviceId) + "\",mac=\"" + WiFi.macAddress().c_str() + "\"}";

  if (hastemp) {
    int stat = bme.readTempC();

    message += "# HELP temperature\n";
    message += "# TYPE temp gauge\n";
    message += "temp";
    message += idString;
    message += String(stat);
    message += "\n";
  }

  if (hashumidity) {
    int stat = bme.readFloatHumidity();

    message += "# HELP humidity value, in percent\n";
    message += "# TYPE humidity gauge\n";
    message += "hum";
    message += idString;
    message += String(stat);
    message += "\n";
  }

  if (haspressure) {
    int stat = bme.readFloatPressure()/100.0F;

    message += "# HELP pressure\n";
    message += "# TYPE pressure gauge\n";
    message += "pressure";
    message += idString;
    message += String(stat);
    message += "\n";
   }
  
  if (hasco2) {
    int stat = myCCS811.getCO2();
    
    message += "# HELP co2, in ppm\n";
    message += "# TYPE co2 gauge\n";
    message += "co2";
    message += idString;
    message += String(stat);
    message += "\n";
  }

  if (hastvoc) {
   int stat = myCCS811.getTVOC();
    
    message += "# HELP tvoc\n";
    message += "# TYPE tvoc gauge\n";
    message += "tvoc";
    message += idString;
    message += String(stat);
    message += "\n";
  }

  return message;
}

void HandleRoot() {
  server.send(200, "text/plain", GenerateMetrics() );
}

void HandleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/html", message);
}

//printSensorError gets, clears, then prints the errors
//saved within the error register.
void printSensorError()
{
        uint8_t error = myCCS811.getErrorRegister();

        if (error == 0xFF) //comm error
        {
                Serial.println("Failed to get ERROR_ID register.");
        }
        else
        {
                Serial.print("Error: ");
                if (error & 1 << 5)
                        Serial.print("HeaterSupply");
                if (error & 1 << 4)
                        Serial.print("HeaterFault");
                if (error & 1 << 3)
                        Serial.print("MaxResistance");
                if (error & 1 << 2)
                        Serial.print("MeasModeInvalid");
                if (error & 1 << 1)
                        Serial.print("ReadRegInvalid");
                if (error & 1 << 0)
                        Serial.print("MsgInvalid");
                Serial.println();
        }
}
