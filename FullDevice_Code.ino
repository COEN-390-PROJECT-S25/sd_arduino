#include <BluetoothSerial.h>
#include <WiFi.h>
//#include <WiFiClient.h>
//#include <WiFiServer.h>
#include <Preferences.h>  //same as android shared preferences
#include <SensirionI2cScd4x.h>
#include <Wire.h>
#include <Adafruit_SGP30.h>
#include <Adafruit_SHT31.h>
#include "time.h"

//data base communication
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define API_KEY "AIzaSyBIII7RhlPA2HgqchRmT1MIBpXXEsHQ7eg"
#define DATABASE_URL "https://sd-helper-db-default-rtdb.firebaseio.com/" // e.g., "your-project-id.firebaseio.com"
#define USER_EMAIL "a@mail.com"
#define USER_PASS "123123"

//time
const char* ntpServer = "pool.ntp.org";

//firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
bool signupOK = false;
const long sendInterval = 30000; //15 seconds

//used to store data like SSID/password for wifi to the chip
Preferences prefs;

// Bluetooth setup
BluetoothSerial SerialBT;

//wifi data
String ssid = "";
String password = "";
bool wifiConfigured = false;

//wifi server
//WiFiServer server(8080);
//WiFiClient client;

//sht31
Adafruit_SHT31 sht31 = Adafruit_SHT31();

//scd41
SensirionI2cScd4x scd41;

static char errorMessage[64];
static int16_t scdError;
bool scd41_data_ready = false;
uint16_t co2_level = 0;
float scd41_temp = 0.0;
float scd41_humidity = 0.0;

//sgp30
Adafruit_SGP30 sgp30;

void setup() {
  /****************  Reconnection setup section START **************************/

  Serial.begin(115200);
  //bluetooth setup
  SerialBT.begin("ESP32_SCHOOL_VER");
  Serial.println("Bluetooth ready!");
  
  //load the wifi details
  prefs.begin("wifi-creds", false);
  ssid = prefs.getString("ssid", "");
  password = prefs.getString("password", "");
  //prefs.clear(); //used to clear your pass/ssid
  prefs.end();

  //if there is a stored internet SSID,
  if (ssid.length() > 0) {
    //try to connect again after bieng powered off.
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.print("Attempting reconnect");
    Serial.println(ssid);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 5000) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiConfigured = true;
      String ip = WiFi.localIP().toString();
      Serial.println("\nAuto-reconnect success! IP: " + ip + "|" + String(WiFi.macAddress()));
      //server.begin();
      //Serial.println("TCP server started on port 8080");
      
      //delay before starting many things.
      delay(2000);
      //start the firebase connection
      initFirebase();
      //disable bluetooth
      SerialBT.end();
      //confirmation
      Serial.println("Bluetooth disabled after auto-reconnect");
    } else {
      Serial.println("\nAuto-reconnect failed, need to use bluetooth");
    }
  } else{
    Serial.println("Need to connect to Internet!");
  }
  /****************  Reconnection section END **************************/

  /****************  harware setup section START ***********************/
  configTime(0, 0, ntpServer);

  Wire.begin();           //i2c setup
  Wire.setClock(100000);  //clock rate

  //scd41
  setup_scd41();
  //sgp30
  if (!sgp30.begin()) {
    Serial.println("SGP30 not found");
    while (1); //prevent continuing
  }

  //sht31 - address is 0x44
  if (!sht31.begin(0x44)) {
    Serial.println("SHT31 not found");
    while (1); //prevent continuing
  }
  /****************  harware setup section END **************************/
}

void loop() {
  //connecting to wifi algorithm
  if (!wifiConfigured && SerialBT.available()) {
    String config = SerialBT.readStringUntil('\n');
    config.trim();
    
    int separatorIndex = config.indexOf('|');
    if (separatorIndex != -1) {
      ssid = config.substring(0, separatorIndex);
      password = config.substring(separatorIndex + 1);
      
      //when you connect to wifi for the first time
      //save the details.
      prefs.begin("wifi-creds", false);
      prefs.putString("ssid", ssid);
      prefs.putString("password", password);
      prefs.end();

      WiFi.begin(ssid.c_str(), password.c_str());
      
      unsigned long startTime = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
        delay(500);
        Serial.print(".");
      }

      //successful connection
      if (WiFi.status() == WL_CONNECTED) {
        //connected bool
        wifiConfigured = true;
        String ip = WiFi.localIP().toString();

        //send the ip directly to the phone.
        SerialBT.println("WIFI_SUCCESS|" + ip + "|" + String(WiFi.macAddress()));
        Serial.println("\nConnected! IP: " + ip);

        //start the local server
        //server.begin(); 
        //Serial.println("TCP server started on port 8080");

        //delay for a second.
        delay(2000);
        //firebase connection
        initFirebase();
        //stop bluetooth
        SerialBT.end();
        //confirmation
        Serial.println("Succesfully disabled bluetooth.");

      } else {
        //failed message send it to the phone.
        SerialBT.println("WIFI_FAILED");
        Serial.println("\nConnection failed!");
      }
    }
  }

  //hardware functionality starts after connection.
  if (wifiConfigured) {
    //send every 15 seconds only
    if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > sendInterval)) {
      sendDataPrevMillis = millis();
      
      //scd41
      scdError = scd41.getDataReadyStatus(scd41_data_ready);
      if (scdError) {
        Serial.print("SCD41 data ready error: ");
        errorToString(scdError, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
      } else if (scd41_data_ready) {
        scdError = scd41.readMeasurement(co2_level, scd41_temp, scd41_humidity);
        if (scdError) {
          Serial.print("SCD41 read error: ");
          errorToString(scdError, errorMessage, sizeof errorMessage);
          Serial.println(errorMessage);
        } else {
          Serial.print("SCD41 - CO2: ");
          Serial.print(co2_level);
          Serial.print(" ppm, Temp: ");
          Serial.print(scd41_temp);
          Serial.print("°C, Humidity: ");
          Serial.print(scd41_humidity);
          Serial.println("%");
        }
      }

      //sgp30
      if (!sgp30.IAQmeasure()) {
        Serial.println("SGP30 measurement failed");
      } else {
        Serial.print("SGP30 - TVOC: ");
        Serial.print(sgp30.TVOC);
        Serial.print(" ppb, eCO2: ");
        Serial.print(sgp30.eCO2);
        Serial.println(" ppm");
      }

      //sht31
      float sht31_temp = sht31.readTemperature();
      float sht31_humidity = sht31.readHumidity();
      if (!isnan(sht31_temp)) {
        Serial.print("SHT31 - Temp: ");
        Serial.print(sht31_temp);
        Serial.print("°C, Humidity: ");
        Serial.print(sht31_humidity);
        Serial.println("%");
      } else {
        Serial.println("SHT31 read failed");
      }
      Serial.println(getTime());
      //separator
      Serial.println("-------------------------");

      //send to firebase
      sendSensorDataToFirebase();
    }

    //it takes 5 seconds per readings
    delay(5000); 
  }
}

void setup_scd41(){
  // Initialize SCD41
  scd41.begin(Wire, SCD41_I2C_ADDR_62);
  
  //wake up/reset
  scdError = scd41.wakeUp();
  if (scdError) {
    Serial.print("SCD41 wakeUp error: ");
    errorToString(scdError, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
  }

  //stop it from measuring
  scdError = scd41.stopPeriodicMeasurement();
  if (scdError) {
    Serial.print("SCD41 stop error: ");
    errorToString(scdError, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
  }

  //restart
  scdError = scd41.reinit();
  if (scdError) {
    Serial.print("SCD41 reinit error: ");
    errorToString(scdError, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
  }

    //start measuring
  scdError = scd41.startPeriodicMeasurement();
  if (scdError) {
    Serial.print("SCD41 start measurement error: ");
    errorToString(scdError, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
  } else {
    Serial.println("SCD41 initialized successfully");
  }

}

void sendSensorDataToFirebase() {
  if (Firebase.ready() && signupOK) {
    // Create a JSON object with all sensor data
    FirebaseJson json;
    
    // SCD41 Data
    json.set("co2", co2_level);
    json.set("temperature", scd41_temp);
    json.set("humidity", scd41_humidity);
    
    // SGP30 Data
    json.set("tvoc", sgp30.TVOC);
    json.set("eco2", sgp30.eCO2);
    
    // SHT31 Data
    float sht31_temp = sht31.readTemperature();
    float sht31_humidity = sht31.readHumidity();
    if (!isnan(sht31_temp)) {
      json.set("sht31_temp", sht31_temp);
      json.set("sht31_humidity", sht31_humidity);
    }

    // Add timestamp
    json.set("timestamp", getTime());

    // Push data to Firebase
    String path = "sensors/" + String(WiFi.macAddress());
    
    if (Firebase.RTDB.pushJSON(&fbdo, path, &json)) {
      Serial.println("Data sent to Firebase successfully");
    } else {
      Serial.println("Failed to send data");
      Serial.println("Reason: " + fbdo.errorReason());
    }
  }
}

void initFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASS;
  
  config.token_status_callback = tokenStatusCallback; // See addons/TokenHelper.h

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Wait for authentication
  Serial.println("Waiting for Firebase auth...");
  while ((auth.token.uid) == "") {
    Serial.print(".");
    delay(1000);
  }
  signupOK = true;
  Serial.println("Firebase connected!");
}

// Function that gets current epoch time
unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}

