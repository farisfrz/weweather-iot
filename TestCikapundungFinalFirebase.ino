#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <BH1750.h>
#include <Adafruit_BMP280.h>
#include <Wire.h>
#include "time.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define WIFI_SSID "Tselhome-628B"
#define WIFI_PASSWORD "60712528"
#define API_KEY "AIzaSyDj92aIh1zniGW5Ps8S7uGswz8pxTw7LO4"
#define DATABASE_URL "weweather1-c9385-default-rtdb.asia-southeast1.firebasedatabase.app"

#define TRIG_PIN 19
#define ECHO_PIN 18
#define DHTPIN 4
#define DHTTYPE DHT22
#define SDA_PIN 21 // BH1750
#define SCL_PIN 22 // BH1750
#define SDA_BMP 33 // BMP280
#define SCL_BMP 32 // BMP280
#define RAINDROP_PIN 5 

//ArahAngin
#define RX2 16
#define TX2 17
const char* arah_angin[] = {
  "Utara", "Timur Laut", "Timur", "Tenggara",
  "Selatan", "Barat Daya", "Barat", "Barat Laut"
};

String statusarahangin = "Tidak Diketahui";

DHT dht(DHTPIN, DHTTYPE);
BH1750 lightMeter;
TwoWire I2CBMP = TwoWire(1);
Adafruit_BMP280 bmp(&I2CBMP);

//Anemometer
volatile byte rpmcount; // hitung signals
volatile unsigned long last_micros;
unsigned long timeold;
unsigned long timemeasure = 60.00; // detik
int countThing = 0;
int GPIO_pulse = 14;               // ESP32 = D14
float rpm, rotasi_per_detik;       // rotasi/detik
float kecepatan_kilometer_per_jam; // kilometer/jam
float kecepatan_meter_per_detik;   //meter/detik
volatile boolean flagAnemometer = false;

void ICACHE_RAM_ATTR rpm_anemometer()
{
  flagAnemometer = true;
}

//Raingauge
const int pin_interrupt = 23;
long int jumlah_tip = 0;
long int temp_jumlah_tip = 0;
float curah_hujan = 0.00;
float milimeter_per_tip = 0.70;
volatile boolean flagRaingauge = false;
String kondisihujan = "Tidak Hujan";

void ICACHE_RAM_ATTR hitung_curah_hujan()
{
  flagRaingauge = true;
}

//KondisiCuaca
String kondisicuaca = "Cerah"; // Default
float lux_threshold_cerah = 25000.0;
float lux_threshold_cerahberawan = 15000.0;
float lux_threshold_berawan = 500.0;
//KondisiAngin
String kondisiangin = "Tidak Berangin";
float batas_kecepatanangin = 0.3; //kmh
//KondisiTMA
const float SAFE_WATER_LEVEL_M = 2.0; // Jarak aman dari sensor ke air (misal 2 meter)
String water_level_status = "Aman";
float jarak_air = 0.00;

//Waktu Penampilan Data
unsigned long previousMillis = 0;
const long interval = 60000; // Interval 1 menit (60 * 1000 ms)

// Firebase Objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

// NTP Time
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200; // GMT+7 (Indonesia)
const int daylightOffset_sec = 0;

// Reset curah hujan variables
int lastDay = -1; // Tracking hari terakhir
unsigned long lastResetTime = 0; // Tracking waktu reset terakhir
const unsigned long RESET_INTERVAL_24H = 24 * 60 * 60 * 1000; // 24 jam dalam ms

unsigned long lastSensorRead = 0;
const unsigned long sensorReadInterval = 15000; // Baca sensor setiap 5 detik


void setup() {
  Serial.begin(9600);
  yield();
  //ArahAngin
  Serial2.begin(9600, SERIAL_8N1, RX2, TX2);
  delay(1000);
  yield();
  //Anemometer
  pinMode(GPIO_pulse, INPUT_PULLUP);
  digitalWrite(GPIO_pulse, LOW);
  detachInterrupt(digitalPinToInterrupt(GPIO_pulse));                         
  attachInterrupt(digitalPinToInterrupt(GPIO_pulse), rpm_anemometer, RISING);
  rpmcount = 0;
  rpm = 0;
  timeold = 0;
  yield();
  //Raingauge
  pinMode(pin_interrupt, INPUT);
  attachInterrupt(digitalPinToInterrupt(pin_interrupt), hitung_curah_hujan, FALLING);
  yield();
  //DHT22
  dht.begin();
  yield();
  //BH1750
  Wire.begin(SDA_PIN, SCL_PIN);
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  yield();
  //BMP280
  I2CBMP.begin(SDA_BMP, SCL_BMP);
  bmp.begin(0x76);
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X1,
                    Adafruit_BMP280::SAMPLING_X4,
                    Adafruit_BMP280::FILTER_X4,
                    Adafruit_BMP280::STANDBY_MS_1000);
  delay(500);
  yield();
  //Raindrop
  pinMode(RAINDROP_PIN, INPUT);
  //Ultrasonic
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  yield();
  //Konek WIFI
  Serial.print("Menghubungkan ke WiFi: ");
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  // WiFi connection dengan timeout dan yield
  int wifi_retry = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retry < 20) {
    delay(500);
    yield(); // Penting untuk WiFi connection
    Serial.print(".");
    wifi_retry++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nBerhasil terhubung ke WiFi!");
    Serial.print("Alamat IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nGagal terhubung ke WiFi.");
  }
  yield();

  // Setup waktu
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  delay(2000);
  yield();

  //INISIASI FIREBAJINGAN (Firabase)
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;
  config.timeout.socketConnection = 45000;
  config.timeout.sslHandshake = 15000;
  config.timeout.rtdbKeepAlive = 20000;
  config.timeout.rtdbStreamReconnect = 12000;
  config.timeout.rtdbStreamError = 10000;
  config.cert.data = nullptr;
  config.cert.file = "";
  config.signer.test_mode = true;
  
  fbdo.setResponseSize(2048); 
  fbdo.setBSSLBufferSize(2048, 512);
  yield();

  if (Serial) {
    Serial.println("Connecting to Firebase...");
  }
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  yield();
  

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase connection successful!");
    signupOK = true;
  } else {
    Serial.printf("Firebase connection failed: %s\n", config.signer.signupError.message.c_str());
  }
  yield();

  previousMillis = millis() - interval + 30000; // Untuk trigger pembacaan pertama setelah 5 detik
}

String getWindDirection() {
  String data;
  String windDirection = "Unknown";
  if (Serial2.available()) {
    data = Serial2.readString();
    int a = data.indexOf("*");
    int b = data.indexOf("#");
    if (a != -1 && b != -1) {
      String s_angin = data.substring(a + 1, b);
      int index = s_angin.toInt() - 1;
      windDirection = (index >= 0 && index < 8) ? arah_angin[index] : "Tidak valid";
    }
  }
  return windDirection;
}

String getCurrentTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return String(millis()); // Fallback ke millis jika NTP gagal
  }
  
  char timeString[30];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M", &timeinfo);
  return String(timeString);
}

void resetRainfallData() {
  jumlah_tip = 0;
  curah_hujan = 0.0;
  kondisihujan = "Tidak Hujan";
}

void checkAndResetRainfall() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    // Jika NTP gagal, pake reset berdasarkan interval 24 jam
    if (millis() - lastResetTime >= RESET_INTERVAL_24H) {
      resetRainfallData();
      lastResetTime = millis();
    }
    return;
  }
  
  // OPSI 1: Reset di tengah malam (00:00)
  if (timeinfo.tm_hour == 0 && timeinfo.tm_min == 0 && lastDay != timeinfo.tm_mday) {
    resetRainfallData();
    lastDay = timeinfo.tm_mday;
    return;
  }
}


void sendDataToFirebase() {
  if (!signupOK || WiFi.status() != WL_CONNECTED) {
    Serial.println("Firebase tidak ready atau WiFi disconnect");
    return;
  }

  // Baca semua sensor
  float suhu = dht.readTemperature();
  delay(200); // Beri jeda antar sensor
  yield();
  float kelembapan = dht.readHumidity();
  delay(200); // Beri jeda antar sensor
  yield();
  float suhu_bmp = bmp.readTemperature();
  delay(200); // Beri jeda antar sensor
  yield();
  float tekanan_bmp = bmp.readPressure() / 100.0F;
  delay(200); // Beri jeda antar sensor
  yield();
  float lux = lightMeter.readLightLevel();
  delay(200); // Beri jeda antar sensor
  yield();
  statusarahangin = getWindDirection();
  delay(200); // Beri jeda antar sensor
  yield();
  
  // Buat JSON object
  FirebaseJson json;
  String timestamp = getCurrentTimestamp();
  yield();
  
  json.set("timestamp", timestamp);
  json.set("suhu_dht", suhu);
  json.set("kelembapan", kelembapan);
  json.set("suhu_bmp", suhu_bmp);
  json.set("tekanan", tekanan_bmp);
  json.set("intensitas_cahaya", lux);
  json.set("kecepatan_angin_kmh", kecepatan_kilometer_per_jam);
  json.set("arah_angin", statusarahangin);
  json.set("curah_hujan_mm", curah_hujan);
  json.set("jarak_air_m", jarak_air);
  json.set("kondisi_cuaca", kondisicuaca);
  json.set("kondisi_hujan", kondisihujan);
  json.set("kondisi_angin", kondisiangin);
  json.set("status_air", water_level_status);
  yield();

  // Path dengan timestamp untuk data historis
  String path = "/Cikapundung/" + String(millis());
  
  Serial.println("Mengirim data ke Firebase...");
  
  if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)) {
    Serial.println("Data berhasil dikirim ke Firebase!");
    Serial.println("Path: " + path);
    
    // Juga update data terbaru di path terpisah untuk akses mudah
    //if (Firebase.RTDB.setJSON(&fbdo, "/latest_data", &json)) {
    //  Serial.println("Latest data updated!");
    //}
    
  } else {
    Serial.println("Gagal mengirim data ke Firebase!");
    Serial.println("Reason: " + fbdo.errorReason());
    
    // Coba reconnect jika gagal
    if (fbdo.errorReason().indexOf("connection") != -1) {
      Serial.println("Mencoba reconnect Firebase...");
      Firebase.reconnectWiFi(true);
      delay(1000);
    }
  }
  yield();
}

void readSensorsAndProcess() {
  //Raingauge - proses interrupt flag
  if (flagRaingauge) {
    curah_hujan += milimeter_per_tip;
    jumlah_tip++;
    delay(100); // Debouncing
    flagRaingauge = false;
    yield();
  }
  
  // Update kondisi hujan
  curah_hujan = jumlah_tip * milimeter_per_tip;
  if (curah_hujan <= 0.0) { 
    kondisihujan = "Tidak Hujan";
  } else if (curah_hujan <= 20.00) {
    kondisihujan = "Hujan Ringan";
  } else if (curah_hujan <= 50.00) {
    kondisihujan = "Hujan Sedang";
  } else if (curah_hujan <= 100.00) {
    kondisihujan = "Hujan Lebat";
  } else if (curah_hujan <= 150.00) { 
    kondisihujan = "Hujan Sangat Lebat"; 
  } else {
    kondisihujan = "Hujan Ekstrem";
  }
  yield();

  //Ultrasonic - dengan timeout yang lebih pendek
  digitalWrite(TRIG_PIN, LOW); 
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); 
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 35000);
  jarak_air = (duration * 0.0343 / 2.0)/100.0;
  water_level_status = (jarak_air < SAFE_WATER_LEVEL_M) ? "Tidak Aman" : "Aman";
  yield();

  //Raindrop dan kondisi cuaca
  int statushujan = !digitalRead(RAINDROP_PIN);
  float lux = lightMeter.readLightLevel();
  yield();
  
  if (statushujan == 1) {
    kondisicuaca = "Hujan";
  } else {
      if (lux >= lux_threshold_cerah) {
          kondisicuaca = "Cerah";
      } else if (lux >= lux_threshold_cerahberawan) {
          kondisicuaca = "Cerah Berawan";
      } else if (lux >= lux_threshold_berawan) {
          kondisicuaca = "Berawan";
      } else {
          kondisicuaca = "Berawan";
      }
  }
  yield();
}


void loop() {
  // Check WiFi connection dengan retry yang lebih efisien
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi terputus, mencoba reconnect...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int retry_count = 0;
    while (WiFi.status() != WL_CONNECTED && retry_count < 10) {
      delay(1000);
      yield();
      Serial.print(".");
      retry_count++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi reconnected!");
    } else {
      Serial.println("\nWiFi reconnection failed, will try again later");
    }
  }
  yield();

  // Check dan reset curah hujan jika perlu
  checkAndResetRainfall();
  yield();

  // Baca sensor dengan interval
  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorRead >= sensorReadInterval) {
    lastSensorRead = currentMillis;
    readSensorsAndProcess();
  }
  yield();

  //Anemometer - proses interrupt flag
  if (flagAnemometer) {
    if (long(micros() - last_micros) >= 5000) {
      rpmcount++;
      last_micros = micros();
    }
    flagAnemometer = false;
    yield();
  }
  
  // Proses kalkulasi kecepatan angin
  if ((millis() - timeold) >= timemeasure * 1000) {
    countThing++;
    detachInterrupt(digitalPinToInterrupt(GPIO_pulse));
    
    rotasi_per_detik = float(rpmcount) / float(timemeasure);
    kecepatan_meter_per_detik = ((-0.0181 * (rotasi_per_detik * rotasi_per_detik)) + (1.3859 * rotasi_per_detik) + 1.4055);
    if (kecepatan_meter_per_detik <= 1.5) {
      kecepatan_meter_per_detik = 0.0;
    }
    kecepatan_kilometer_per_jam = kecepatan_meter_per_detik * 3.6;
    
    timeold = millis();
    rpmcount = 0;
    
    attachInterrupt(digitalPinToInterrupt(GPIO_pulse), rpm_anemometer, RISING);
    yield();
  }

  //KondisiAngin
  kondisiangin = (kecepatan_kilometer_per_jam < batas_kecepatanangin) ? "Tidak Berangin" : "Berangin";
  yield();
  
  // Kirim data ke Firebase setiap interval yang ditentukan
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    sendDataToFirebase();
  }
  
  // Main loop delay - sangat penting untuk stabilitas
  delay(1000); // 1 detik delay di main loop
  yield();
}