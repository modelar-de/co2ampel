//#include <SPI.h>  // not used here, but needed to prevent a RTClib compile error
#include <Arduino.h>
#include <ArduinoJson.h>
#include "RTClib.h"
#include <SparkFun_SCD30_Arduino_Library.h>
#include <Adafruit_NeoPixel.h>
#include <FS.h>   //"SPIFFS.h"
#include <Wire.h> //I2C library
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#define DEBUG_LAM // Uncomment to enable Debug Output via Serial
#define CONFIG    // Uncomment to overwrite Esp8266 Standard Configuration

#ifdef DEBUG_LAM
#define LOG(x) Serial.print(x)
#define LOG_ENC(x, y) Serial.print(x, y)
#define LOG_LN(x) Serial.println(x)
#define LOG_LN_ENC(x, y) Serial.println(x, y)
#else
#define LOG(x)
#define LOG_ENC(x, y)
#define LOG_LN(x)
#define LOG_LN_ENC(x, y)
#endif

File myfile; // SPIFFS File Handling Variable
bool RTCactive = true;
unsigned long fallbacktime = 1604188800;
RTC_DS3231 rtc;
DateTime now;
DateTime fallback;
Adafruit_NeoPixel WSpixels = Adafruit_NeoPixel((24 < 24) ? 24 : 24, 15, NEO_GRB + NEO_KHZ800); // ports: pcb 14, diy 15
int dayOfTheWeek = 0;
unsigned long previousMillis = 0;
const long interval = 2000;
const long stationTimeout = 5000;
String devicename="Ampel";

// Network Credentials
String ssid = "";
String password = "";
String myssid = "CO2Ampel";
String mypassword = "ampel2021";
#ifdef CONFIG
IPAddress apIPv4(192, 168, 11, 4); // IP for Esp8266 Access Point
IPAddress apGateway(192, 168, 11, 1);
IPAddress apSubnet(255, 255, 255, 0); // Network Submask
#endif
bool stationmode = false;
//const char *ESPHostname = "ESP";
//String host = "co2ampel1";
bool wifiConnected = false;
int retryCounter = 0;
#define MAXRETRIES 20
String setWifiAnswer = "";

AsyncWebServer server(80);

// SDC30 Sensor
SCD30 airSensor;
int co2_level;
int loopCounter = 0;
int co2LoopAverage = 0;
int logCycle = 10;
int sensorCO2 = 0;
float sensorTemperature = 0.0;
float sensorHumidity = 0.0;
String sensorTime = "";
String today = "";
int limit_1 = 1000;
int limit_2 = 2000;
int tempOffset = 2;
int altitude = 535;
int pressure = 1017;

int bright = 24;
static const char TEXT_PLAIN[] PROGMEM = "text/plain";
static const char FS_INIT_ERROR[] PROGMEM = "FS INIT ERROR";
static const char FILE_NOT_FOUND[] PROGMEM = "FileNotFound";
char *logFiles[] = {"/logSunday.csv", "/logMonday.csv", "/logTuesday.csv", "/logWednesday.csv", "/logThursday.csv", "/logFriday.csv", "/logSaturday.csv"};

//--------- Neopixel Messanzeige (Gauge)
void WSGauge(float val, float limit1, float limit2, float delta, int seg)
{
  float current = 0;
  int shift_value = 0; // NeoPixel on top of ring
  int shifted_pixel = 21;
  for (int i = 0; i <= (seg - 1); i++)
  { // all pixels
    current = (i + 1) * delta;
    shifted_pixel = (i + shift_value) % 24;
    if ((val >= current) && (val < limit1)) // green
      WSpixels.setPixelColor(shifted_pixel, 0, bright, 0);
    else if ((val >= current) && (val <= limit2)) // yellow
      WSpixels.setPixelColor(shifted_pixel, bright / 2, bright / 2, 0);
    else if ((val >= current) && (val > limit2)) // red
      WSpixels.setPixelColor(shifted_pixel, bright, 0, 0);
    else
      WSpixels.setPixelColor(shifted_pixel, 0, 0, 0);
  }
  WSpixels.show();
}

void WSStatusDisplay(int r, int g, int b, int nr, int seg)
{
  for (int i = 0; i < nr; i++)
  {
    WSpixels.setPixelColor(i, 0, 0, bright / 2);
  }
  WSpixels.show();

  delay(500);
}

String getCO2()
{
  float co2 = airSensor.getCO2();
  return String((int)co2);
}

String getTemperature()
{
  float temperature = airSensor.getTemperature();
  return String(temperature);
}

String getHumidity()
{
  float humidity = airSensor.getHumidity();
  return String(humidity);
}

void getData()
{
  if (RTCactive)
    now = rtc.now();
  else
    now = DateTime(fallbacktime + millis() / 1000L);
  char buf_hms[] = "hh:mm:ss";
  sensorTime = now.toString(buf_hms);
  sensorCO2 = (int)airSensor.getCO2();
  sensorTemperature = airSensor.getTemperature();
  sensorHumidity = airSensor.getHumidity();
  if (now.dayOfTheWeek() != dayOfTheWeek)
  {
    LOG_LN("New day -> new log");
    LOG_LN("Clear Old Log");
    char buf_ymd[] = "YYYYMMDD";
    today = now.toString(buf_ymd);
    myfile = SPIFFS.open(logFiles[now.dayOfTheWeek()], "w");
    myfile.println(today);
    myfile.close();
  }
  dayOfTheWeek = now.dayOfTheWeek();
}

String getSensorData()
{
  /*DateTime now = rtc.now();
  char buf_hms[] = "hh:mm:ss";
  String timeNow = now.toString(buf_hms);*/
  //String tmp = "{\"Time\":\"" + timeNow + "\", \"CO2\":\"" + getCO2() + "\", \"Temperature\":\"" + getTemperature() + "\", \"Humidity\":\"" + getHumidity() + "\"}";
  return "{\"Time\":\"" + sensorTime + "\", \"CO2\":\"" + sensorCO2 + "\", \"Temperature\":\"" + sensorTemperature + "\", \"Humidity\":\"" + sensorHumidity + "\"}";
  //return String(tmp);
}

// Replaces placeholder with stored values
String processor(const String &var)
{
  LOG_LN(var);
  if (var == "LIMIT1")
  {
    return (String)limit_1;
  }
  else if (var == "LIMIT2")
  {
    return (String)limit_2;
  }
  else if (var == "DEVICENAME")
  {
    LOG_LN (devicename);
    return (String)devicename;
  } 
}

bool loadConfig()
{
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile)
  {
    LOG_LN("Failed to open config file");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024)
  {
    LOG_LN("Config file size is too large");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  StaticJsonDocument<300> doc;
  auto error = deserializeJson(doc, buf.get());
  if (error)
  {
    LOG_LN("Failed to parse config file");
    return false;
  }
  limit_1 = doc["limit1"].as<int>();
  limit_2 = doc["limit2"].as<int>();
  pressure = doc["pressure"].as<int>();
  altitude = doc["altitude"].as<int>();
  tempOffset = doc["tempoffset"].as<int>();
  ssid = doc["ssid"].as<String>();
  password = doc["password"].as<String>();
  myssid = doc["myssid"].as<String>();
  mypassword = doc["mypassword"].as<String>();
  fallbacktime = doc["fallbacktime"].as<unsigned long>();
  stationmode = doc["stationmode"].as<bool>();
  LOG_LN("Limit_1: " + (String)limit_1);
  LOG_LN("Limit_2: " + (String)limit_2);
  devicename = doc["devicename"].as<String>();
  String output;
  serializeJson(doc, output);
  LOG_LN(output);

  return true;
}

bool saveConfig()
{
  StaticJsonDocument<300> doc;
  doc["limit1"] = limit_1;
  doc["limit2"] = limit_2;
  doc["pressure"] = pressure;
  doc["altitude"] = altitude;
  doc["tempoffset"] = tempOffset;
  doc["ssid"] = ssid;
  doc["password"] = password;
  doc["myssid"] = myssid;
  doc["mypassword"] = mypassword;
  //doc["host"] = host;
  doc["fallbacktime"] = fallbacktime;
  doc["stationmode"] = stationmode;
  doc["devicename"] = devicename;
  String output;
  serializeJson(doc, output);
  LOG_LN(output);


  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile)
  {
    LOG_LN("Failed to open config file for writing");
    return false;
  }

  serializeJson(doc, configFile);
  return true;
}

void initLog()
{
  // Creates empty log files at first start
  for (int i = 0; i < 7; i++)
  {
    if (!(SPIFFS.exists(logFiles[i])))
    {
      myfile = SPIFFS.open(logFiles[i], "w");
      if (!myfile)
      {
        LOG("Error writing file ");
        LOG_LN(logFiles[i]);
      }
      myfile.println("20011212");
      myfile.println(devicename);
      LOG("File created: ");
      LOG_LN(logFiles[i]);
      myfile.close();
    }
  }

  // Check if log for the current day needs to be cleared
  if (RTCactive)
    now = rtc.now();
  else
    now = DateTime(fallbacktime);
  char buf_ymd[] = "YYYYMMDD";
  String today = now.toString(buf_ymd);
  dayOfTheWeek = now.dayOfTheWeek();
  delay(1000);
  myfile = SPIFFS.open(logFiles[dayOfTheWeek], "r");
  String logdate = myfile.readStringUntil('\n');
  logdate.trim();
  LOG_LN(today + "-" + logdate);
  myfile.close();
  if (!logdate.equals(today))
  {
    LOG_LN("Clear Old Log");
    myfile = SPIFFS.open(logFiles[now.dayOfTheWeek()], "w");
    myfile.println(today);
    myfile.println(devicename);
    myfile.close();
  }
  else
  {
    LOG_LN("Resume Log");
    myfile = SPIFFS.open(logFiles[now.dayOfTheWeek()], "a");
    myfile.println("");
    myfile.println("");
    myfile.close();
  }
}

void initSCD30()
{
  Wire.begin(); // ---- Initialisiere den I2C-Bus
  if (Wire.status() != I2C_OK)
    LOG_LN("Something wrong with I2C");
  if (airSensor.begin() == false)
  {
    LOG_LN("The SCD30 did not respond. Please check wiring.");
    while (1)
    {
      yield();
      delay(1);
    }
  }
  airSensor.setAltitudeCompensation(altitude);
  airSensor.setAmbientPressure(pressure);
  airSensor.setTemperatureOffset(tempOffset);

  airSensor.setAutoSelfCalibration(false); // Sensirion no auto calibration
  airSensor.setMeasurementInterval(2);     // CO2-Measurement every 2 s
  Wire.setClock(100000L);                  // 100 kHz SCD30
  Wire.setClockStretchLimit(200000L);      // CO2-SCD30
  co2_level = 0;

  LOG_LN("SCD30 initialized");
  delay(1000);
}

String calibrateSCD30()
{
  // Forced Calibration Sensirion SCD 30
  airSensor.setAltitudeCompensation(altitude);
  airSensor.setAmbientPressure(pressure);
  airSensor.setForcedRecalibrationFactor(400); // fresh air
  return "Kalibrierung erfolgt!";
}

void initRTC()
{
  if (!rtc.begin())
  {
    LOG_LN("Couldn't find RTC");
    RTCactive = false;

    //while (1)
    //Serial.flush();
    //abort();
  }

  if (rtc.lostPower())
  {
    LOG_LN("RTC lost power, lets set the time!");
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }
}

bool connectWifiSTA()
{
  WiFi.begin(ssid, password);
  long timewait = 0;
  long timeout = 1000;
  while (WiFi.status() != WL_CONNECTED && timewait < timeout)
  {
    LOG(".");
    delay(500);
    timewait = timewait + 500;
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    wifiConnected = true;
    LOG_LN("Connected to Network: " + String(ssid));
    LOG_LN("Enter the following IP in your Browser: " + WiFi.localIP().toString());
  }
}

void connectWiFi()
{
  WiFi.hostname(myssid);
  WiFi.mode(WIFI_AP_STA);
  wifiConnected = false;
  retryCounter = 0;
  WiFi.begin(ssid, password);
  yield();

#ifdef CONFIG
  WiFi.softAPConfig(apIPv4, apGateway, apSubnet);
#endif
  if (WiFi.softAP(myssid, mypassword))
  {
    LOG_LN("Set up as Access Point: " + String(myssid));
    LOG_LN("Enter the following IP in your Browser: " + WiFi.softAPIP().toString());
  }
  else
  {
    LOG_LN("Error Setting up WiFi.");
  }
}

void notFound(AsyncWebServerRequest *request)
{

  if (request->url().endsWith(F(".csv")))
  {
    // here comes some mambo-jambo to extract the filename from request->url()
    int fnsstart = request->url().lastIndexOf('/');
    String fn = request->url().substring(fnsstart);
    // ... and finally
    request->send(SPIFFS, fn, String(), true);
  }
  else
  {
    request->send_P(404, PSTR("text/plain"), PSTR("Not found"));
  }
}

void initServer()
{
  server.onNotFound(notFound);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/style.css", "text/css");
  });
  server.on("/chartist.min.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/chartist.min.css", "text/css");
  });
  server.on("/chartist.min.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/chartist.min.js", "text/javascript");
  });
  server.on("/chartistzoom.min.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/chartistzoom.min.js", "text/javascript");
  });
  server.on("/moment.min.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/moment.min.js", "text/javascript");
  });
  server.on("/segment-display.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/segment-display.js", "text/javascript");
  });
  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", getTemperature().c_str());
  });
  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", getHumidity().c_str());
  });
  server.on("/co2", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", getCO2().c_str());
  });
  server.on("/sensordata", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", getSensorData().c_str());
  });
  server.on("/calibrateSCD30", HTTP_GET, [](AsyncWebServerRequest *request) {
    String retStr = "Bitte Passwort eingeben...";
    if (request->hasParam("pw"))
    {
      if (request->getParam("pw")->value() != mypassword)
      {
        retStr = "Passwort fehlerhaft...";
      }
      else
        retStr = calibrateSCD30();
    }
    request->send_P(200, "text/plain", retStr.c_str());
  });
  server.on("/setDeviceName", HTTP_GET, [](AsyncWebServerRequest *request) {
    String retStr = "Bitte Name eingeben!";
    if (request->hasParam("name"))
    {
      if (request->getParam("name")->value() != "")
      {
        devicename = request->getParam("name")->value();
        LOG_LN(devicename);
        myssid = "CO2-Ampel_" + devicename;
        saveConfig();
        retStr = "Gerätebezeichnung geändert: " + devicename;
      }
    }
    request->send_P(200, "text/plain", retStr.c_str());
  });
  server.on("/setLimits", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("limit1") && request->hasParam("limit2"))
    {
      limit_1 = (request->getParam("limit1")->value()).toInt();
      limit_2 = (request->getParam("limit2")->value()).toInt();
      saveConfig();
      LOG_LN("Limits changed: " + String(limit_1) + "/" + String(limit_2));
      request->send_P(200, "text/plain", "Success");
    }
    else
      request->send_P(200, "text/plain", "Error");
  });
  server.on("/setTime", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("date") && request->hasParam("time"))
    {
      String tim = request->getParam("time")->value();
      String dat = request->getParam("date")->value();
      if (RTCactive)
        rtc.adjust(DateTime((dat.substring(0, 4)).toInt(), (dat.substring(5, 7)).toInt(), (dat.substring(8, 10)).toInt(),
                            (tim.substring(0, 2)).toInt(), (tim.substring(3, 5)).toInt(), 0));
      else
        fallback = DateTime((dat.substring(0, 4)).toInt(), (dat.substring(5, 7)).toInt(), (dat.substring(8, 10)).toInt(),
                            (tim.substring(0, 2)).toInt(), (tim.substring(3, 5)).toInt(), 0);
      saveConfig();
      LOG_LN("Time changed: " + tim + "/" + dat);
      request->send_P(200, "text/plain", "Success");
    }
    else
      request->send_P(200, "text/plain", "Error");
  });
  server.on("/setWifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    LOG_LN("setWIFI");
    if (request->hasParam("ssid") && request->hasParam("pass"))
    {

      ssid = request->getParam("ssid")->value();
      password = request->getParam("pass")->value();
      LOG_LN(password);
      LOG_LN(ssid);
    }
    if (request->hasParam("connect"))
    {
      if (request->getParam("connect")->value() == "off")
        stationmode = false;
      else
        stationmode = true;
    }
    saveConfig();
    request->send_P(200, "text/plain", "Zugangsdaten gespeichert. Bitte warten...");
  });
  server.on("/getWifiResult", HTTP_GET, [](AsyncWebServerRequest *request) {
    LOG_LN("getWIFIResult");
    request->send_P(200, "text/plain", setWifiAnswer.c_str());
    setWifiAnswer = "";
  });

  server.begin(); // finally start server
}

void setup()
{
  // Open Serial Connection
  Serial.begin(115200);
  // Initializing Neopixel
  WSpixels.begin();
  delay(1000);
  WSStatusDisplay(0, 0, 255, 4, 24);

  // Setup RTC
  initRTC();

  WSStatusDisplay(0, 0, 255, 8, 24);

  // Mount SPIFFS File System
  if (!SPIFFS.begin())
  {
    LOG_LN("An Error has occurred while mounting SPIFFS");
    return;
  }
  delay(500);
  loadConfig();
  if (!RTCactive)
  {
    fallback = DateTime(fallbacktime);
    fallbacktime += 86400;
    saveConfig();
  }
  // Initialize Logs
  initLog();

  WSStatusDisplay(0, 0, 255, 12, 24);

  // Start WiFi
  connectWiFi();

  WSStatusDisplay(0, 0, 255, 16, 24);

  // Initialize SCD30
  initSCD30();

  WSStatusDisplay(0, 0, 255, 20, 24);

  // Initialize Server
  initServer();

  WSStatusDisplay(0, 0, 255, 24, 24);

  delay(1000);

  LOG_LN("Initialization finished");
}

void loop()
{
  //loop without delay
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval)
  {
    // save the last time data was retrieved
    previousMillis = currentMillis;

    getData();
    //LOG_LN_ENC(ESP.getFreeHeap(), DEC);
    ;
    WSGauge(sensorCO2, limit_1, limit_2, 100, 24);
    co2LoopAverage += sensorCO2;
    /* LOG("data: " + String(sensorTime) + " / " + String(sensorCO2) + " / " + String(sensorTemperature) + " / " + String(sensorHumidity));
    LOG("co2_level: " + String(sensorCO2));
    LOG_LN(); */

    if (stationmode)
    {

      WiFi.disconnect();
      delay(500);
      WiFi.begin(ssid, password);
      delay(500);
      int connRes = WiFi.waitForConnectResult();
      LOG("connRes: ");
      LOG_LN(connRes);
      int i = 0;

      while ((connRes == 0) and (i != 10)) //if connRes == 0  "IDLE_STATUS - change Statius"
      {
        connRes = WiFi.waitForConnectResult();
        delay(1000);
        i++;
        LOG_LN(".");
        // statement(s)
      }
      while ((connRes == 1) and (i != 10)) //if connRes == 1  NO_SSID_AVAILin - SSID cannot be reached
      {
        connRes = WiFi.waitForConnectResult();
        setWifiAnswer = "SSID nicht erreichbar";
        delay(1000);
        i++;
        LOG_LN(".");
        // statement(s)
      }
      if (connRes == 3)
      {
        LOG("STA ");
        WiFi.setAutoReconnect(true); // Set whether module will attempt to reconnect to an access point in case it is disconnected.
/*         // Setup MDNS responder
        if (!MDNS.begin(ESPHostname))
        {
          LOG_LN("Err: MDNS");
        }
        else
        {
          MDNS.addService("http", "tcp", 80);
        } */
        setWifiAnswer = "Verbindung erfolgreich! IP-Adresse: " + WiFi.localIP().toString();
        LOG_LN(WiFi.localIP().toString());
      }
      if (connRes == 4)
      {
        LOG_LN("STA Pwd Err");
        setWifiAnswer = "Passwort falsch!";
        //LOG("PwLen:");
        //LOG_LN(strlen(MyWiFiConfig.WiFiPwd));
        //LOG("PwSize");
        //LOG_LN(sizeof(MyWiFiConfig.WiFiPwd));
        LOG_LN(ssid);
        LOG_LN(password);
        WiFi.disconnect();
      }

      if (connRes == 6)
      {
        setWifiAnswer = "DISCONNECTED - Not in station mode!";
        LOG_LN("DISCONNECTED - Not in station mode");
      }
      WiFi.printDiag(Serial);
      stationmode = false;
    }

    /* if (stationmode && !wifiConnected && retryCounter == 0)
    {
      WiFi.begin(ssid, password);
      LOG_LN("Trying to connect...");
      retryCounter++;
    }
    else
    {
      if (WiFi.status() == WL_CONNECTED)
      {
        wifiConnected = true;
        if (retryCounter > 0)
        {
          LOG_LN("Connected to Network: " + String(ssid));
          LOG_LN("Enter the following IP in your Browser: " + WiFi.localIP().toString());
        }
        retryCounter = 0;
      }
      else
      {
        if (wifiConnected)
        {
          wifiConnected = false;
          retryCounter = 0;
        }
        else
        {
          LOG_LN("Still trying...");
          if (retryCounter < MAXRETRIES)
            retryCounter++;
          else
            retryCounter = 0;
        }
      }
    } */

    yield();
    if (loopCounter == logCycle - 1)
    {
      loopCounter = 0;
      myfile = SPIFFS.open(logFiles[dayOfTheWeek], "a");
      myfile.print(sensorTime);
      myfile.print(",");
      myfile.println(String((int)(co2LoopAverage / logCycle)));
      myfile.close();
      LOG_LN();
      LOG_LN(String(logFiles[dayOfTheWeek]) + " " + sensorTime + " " + String((int)(co2LoopAverage / logCycle)));
      co2LoopAverage = 0;
    }
    else
    {
      loopCounter++;
      //LOG(loopCounter);
      // LOG_LN(" " + String(co2LoopAverage));
    }
  }

  //delay(2000);
}
