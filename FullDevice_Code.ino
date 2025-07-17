#include <BluetoothSerial.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <DHT.h>
#include <Preferences.h>  // For persistent storage

//used to store data like SSID/password for wifi.
Preferences prefs;

// Bluetooth setup
BluetoothSerial SerialBT;

//wifi data
String ssid = "";
String password = "";


//wifi server
WiFiServer server(8080);
WiFiClient client;
bool wifiConfigured = false;

//dht22
#define DHTPIN 4     //pin 4 on esp32
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(115200);
  //bluetooth setup
  SerialBT.begin("ESP32_SCHOOL_VER");
  Serial.println("Bluetooth ready!");
  //dht sensor setup
  dht.begin();

   // Load saved Wi-Fi credentials
  prefs.begin("wifi-creds", false);  // Open namespace in read/write mode
  ssid = prefs.getString("ssid", "");
  password = prefs.getString("password", "");
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
      Serial.println("\nAuto-reconnect success! IP: " + ip);
      server.begin();
      Serial.println("TCP server started on port 8080");
      
      // Disable Bluetooth if auto-reconnected
      delay(2000);
      SerialBT.end();
      Serial.println("Bluetooth disabled after auto-reconnect");
    } else {
      Serial.println("\nAuto-reconnect failed, need to use bluetooth");
    }
  } 
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
      

      // Save credentials to flash
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

      if (WiFi.status() == WL_CONNECTED) {
        //set it to true
        wifiConfigured = true;
        String ip = WiFi.localIP().toString();

        //send the ip directly to the phone.
        SerialBT.println("WIFI_SUCCESS|" + ip);
        Serial.println("\nConnected! IP: " + ip);

        //start the local server
        server.begin(); 
        Serial.println("TCP server started on port 8080");

        //stop the bluetooth connection
        delay(5000);
        SerialBT.end();
        Serial.println("Succesfully disabled bluetooth.");

      } else {
        //failed message send it to the phone.
        SerialBT.println("WIFI_FAILED");
        Serial.println("\nConnection failed!");
      }
    }
  }

  //if connected
  if (wifiConfigured) {
    //check for the phone to be connected.
    if (!client || !client.connected()) {
      client = server.available();
      if (client) {
        Serial.println("New client connected");
      }
    } 
    
    //dht22 sensor data reading and output
    float humidity = dht.readHumidity();
    float tempC = dht.readTemperature(); 
    
    if (isnan(humidity) || isnan(tempC)) {
      Serial.println("DHT22 read error!");
    } else {
      //[rint readings]
      Serial.print("Humidity: "); Serial.print(humidity); Serial.print("%");
      Serial.print(" | Temp: "); Serial.print(tempC); Serial.println("°C");
      
      //send it to the phone.
      if (client && client.connected()) {
        //compact data
        String data = "Temp:" + String(tempC) + "°C,Humidity:" + String(humidity) + "%\n";
        //send it to the phone
        client.print(data);
      }
    }
    
    delay(2000); // Wait 2 seconds between readings
  }
}