#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// تنظیمات Wi-Fi
const char* ssid = "Redmi Note 12S";  // نام شبکه
const char* password = "13801380";  // رمز عبور

// آدرس API سرور
const String apiBaseUrl = "https://smartpot.runasp.net/api";

// تعریف پین‌های سخت‌افزاری
#define RX_PIN D6
#define TX_PIN D7
#define DHTPIN D5
#define DHTTYPE DHT11
#define MOISTURE_PIN A0
#define RELAY_PIN D1

// تعریف متغیرها و کلاس‌ها
DHT dht(DHTPIN, DHTTYPE);
SoftwareSerial mySoftwareSerial(RX_PIN, TX_PIN);
DFRobotDFPlayerMini myDFPlayer;

// تایمرها
unsigned long previousPumpMillis = 0;   // تایمر برای بررسی وضعیت پمپ
unsigned long previousSensorMillis = 0; // تایمر برای ارسال داده‌ها
const unsigned long pumpInterval = 500; // بررسی وضعیت پمپ هر 500 میلی‌ثانیه
const unsigned long sensorInterval = 15000; // ارسال داده‌ها هر 15 ثانیه

// وضعیت فعلی پمپ
bool currentPumpStatus = false;
int pumpCheckCounter = 0;  // شمارنده برای چک کردن وضعیت پمپ

void setup() {
  Serial.begin(115200);
  mySoftwareSerial.begin(9600);

  // تنظیم پین‌ها
  dht.begin();
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // پمپ در ابتدا خاموش باشد

  // اتصال به Wi-Fi
  connectToWiFi();
}

void loop() {
  unsigned long currentMillis = millis();

  // بررسی وضعیت پمپ هر 500 میلی‌ثانیه
  if (currentMillis - previousPumpMillis >= pumpInterval) {
    previousPumpMillis = currentMillis;
    controlPumpFromApi();
    pumpCheckCounter++;  // افزایش شمارنده برای چک کردن پمپ

    // اگر شمارنده به 7 رسید، داده‌ها ارسال شوند
    if (pumpCheckCounter >= 7) {
      sendSensorData();  // ارسال داده دوم
      sendSensorData();  // ارسال داده اول
      pumpCheckCounter = 0;  // شمارنده را دوباره صفر کنیم
    }
  }

  // بررسی رطوبت خاک و روشن کردن پمپ به صورت خودکار
  checkSoilMoistureAndControlPump();
}

// تابع اتصال به Wi-Fi
void connectToWiFi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi!");
}

// تابع ارسال داده‌های سنسورها به API
void sendSensorData() {
  int moistureValue = analogRead(MOISTURE_PIN);
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  Serial.printf("Temperature: %.2f °C, Humidity: %.2f %%, Soil Moisture: %d\n",
                temperature, humidity, moistureValue);

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    String url = apiBaseUrl + "/sensor/log";
    http.begin(secureClient, url);

    StaticJsonDocument<128> doc;
    doc["temperature"] = temperature;
    doc["humidity"] = humidity;
    doc["soilMoisture"] = moistureValue;
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode > 0) {
      Serial.println("Data sent successfully!");
    } else {
      Serial.printf("Error sending data: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    http.end();
  } else {
    Serial.println("WiFi disconnected. Skipping data send.");
    connectToWiFi();
  }
}

// تابع بررسی وضعیت پمپ از API
void controlPumpFromApi() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    String url = apiBaseUrl + "/Pump/status";
    http.begin(secureClient, url);

    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.printf("Pump Status Response: %s\n", response.c_str());

      StaticJsonDocument<128> doc;
      DeserializationError error = deserializeJson(doc, response);
      if (error) {
        Serial.printf("JSON parse error: %s\n", error.f_str());
        return;
      }

      bool pumpStatus = doc["status"];
      if (pumpStatus != currentPumpStatus) {
        currentPumpStatus = pumpStatus;
        digitalWrite(RELAY_PIN, pumpStatus ? HIGH : LOW);
        Serial.println(pumpStatus ? "Pump turned ON" : "Pump turned OFF");
      }
    } else {
      Serial.printf("Error fetching pump status: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    http.end();
  } else {
    Serial.println("WiFi disconnected. Cannot fetch pump status.");
    connectToWiFi();
  }
}

// تابع بررسی رطوبت خاک و کنترل پمپ
void checkSoilMoistureAndControlPump() {
  int soilMoistureValue = analogRead(MOISTURE_PIN);
  Serial.printf("Soil Moisture Value: %d\n", soilMoistureValue);

  // بررسی اگر رطوبت کمتر از حد مجاز باشد
  const int MOISTURE_THRESHOLD = 300; // مقدار آستانه برای رطوبت خاک
  if (soilMoistureValue < MOISTURE_THRESHOLD) {
    Serial.println("Soil is too dry. Turning pump ON for 10 seconds.");
    digitalWrite(RELAY_PIN, HIGH); // روشن کردن پمپ
    delay(10000); // پمپ به مدت 10 ثانیه روشن باشد
    digitalWrite(RELAY_PIN, LOW); // خاموش کردن پمپ
    Serial.println("Pump turned OFF after 10 seconds.");
  }
}
