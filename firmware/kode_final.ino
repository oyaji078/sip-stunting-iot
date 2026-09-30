/*
===============================================================
 SISTEM IoT DETEKSI DINI STUNTING BALITA
 BERBASIS Z-SCORE WHO (TB/U)

 PLATFORM : ESP32
 TARGET   : Balita 24 - 60 bulan

 FUNGSI:
 - RFID RC522 untuk identifikasi balita
 - HX711 + 4 half-bridge load cell untuk berat
 - VL53L1X untuk tinggi
 - LCD 20x4
 - Buzzer
 - WiFi
 - Komunikasi PHP/MySQL melalui API

===============================================================
 PIN FINAL
===============================================================

 TOF VL53L1X
 SDA -> GPIO 21
 SCL -> GPIO 22

 LCD 20x4 PCF8574
 SDA -> GPIO 32
 SCL -> GPIO 33

 HX711
 DT  -> GPIO 16
 SCK -> GPIO 4

 RFID RC522
 SDA/SS -> GPIO 5
 SCK    -> GPIO 18
 MOSI   -> GPIO 19
 MISO   -> GPIO 23
 RST    -> GPIO 27

 BUZZER
 GPIO 25

===============================================================
 KALIBRASI BERAT
===============================================================

 Faktor kalibrasi:
 27312.27

 Sensor tinggi dari lantai:
 149.50 cm

===============================================================
 CATATAN PENTING
===============================================================

 Tinggi TIDAK dibulatkan sebelum perhitungan WHO LMS.

 Contoh:
 Sensor membaca 88.60 cm

 Yang dikirim ke API:
 88.60 cm

 BUKAN:
 89 cm

 PHP/MySQL menggunakan tinggi presisi tersebut
 untuk menghitung Z-Score WHO.

===============================================================
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include "HX711.h"
#include <VL53L1X.h>

// =============================================================
// WIFI
// =============================================================

const char* API_BASE_URL = "https://posyandu.oypoint.online/api/v1";
const char* FIRMWARE_VERSION = "2.1.0";

// ISRG Root X1 (Let's Encrypt), valid until 2035.
const char* ROOT_CA = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)EOF";
Preferences preferences;
String deviceId;
String deviceToken;
int authFailureCount = 0;

// Satu koneksi HTTPS dipakai ulang (keep-alive) untuk semua request. Handshake
// TLS di ESP32 memakan 1-3 detik; dibuat baru tiap request membuat loop macet
// dan kartu RFID terasa sangat lambat terbaca.
WiFiClientSecure secureClient;
bool secureClientReady = false;
unsigned long lastHeartbeatAt = 0;
unsigned long lastPendingRetryAt = 0;
bool pendingMeasurement = false;
String pendingUid;
String pendingRequestId;
float pendingWeight = 0;
float pendingHeight = 0;
bool rfidReady = false;
bool scaleReady = false;
bool tofReady = false;
bool lcdReady = false;

// Mode tangkap kartu dari website: "off", "register" (daftar balita),
// atau "select" (pilih balita untuk pemeriksaan). Saat aktif, kartu yang
// ditempel hanya dikirim ke web dan TIDAK memulai penimbangan.
String captureMode = "off";
unsigned long lastModePollAt = 0;
unsigned long modePollInterval = 1000;
unsigned long lastRFIDHealthCheckAt = 0;
const unsigned long RFID_HEALTH_CHECK_INTERVAL = 5000;

enum RFIDCheckResult
{
  RFID_REGISTERED,
  RFID_UNKNOWN,
  RFID_INELIGIBLE,
  RFID_CAPTURED,
  RFID_CAPTURED_REGISTERED,
  RFID_API_ERROR
};

// =============================================================
// PIN
// =============================================================

// ---------------- TOF ----------------
#define TOF_SDA 21
#define TOF_SCL 22

// ---------------- LCD ----------------
#define LCD_SDA 32
#define LCD_SCL 33

// ---------------- HX711 ----------------
#define HX711_DT  16
#define HX711_SCK 4

// ---------------- RFID ----------------
#define RFID_SS   5
#define RFID_RST  27

#define RFID_SCK  18
#define RFID_MISO 23
#define RFID_MOSI 19

// ---------------- BUZZER ----------------
#define BUZZER_PIN 25

// =============================================================
// OBJEK SENSOR
// =============================================================

HX711 scale;
VL53L1X tof;

MFRC522 rfid(RFID_SS, RFID_RST);

// I2C khusus LCD
TwoWire I2C_LCD = TwoWire(1);

// =============================================================
// KONFIGURASI LCD PCF8574
// =============================================================

#define LCD_ADDRESS 0x27

#define LCD_RS 0x01
#define LCD_RW 0x02
#define LCD_EN 0x04
#define LCD_BL 0x08

// =============================================================
// KALIBRASI HX711
// =============================================================

const float CALIBRATION_FACTOR = 27312.27;

// =============================================================
// KONFIGURASI BERAT
// =============================================================

const float MIN_WEIGHT = 2.00;

// toleransi kestabilan
const float STABLE_TOLERANCE = 0.10;

// harus stabil selama 1.5 detik
const unsigned long STABLE_TIME = 1500;

// setelah LOCK ditampilkan
const unsigned long WEIGHT_LOCK_TIME = 2000;

// =============================================================
// KONFIGURASI TINGGI
// =============================================================

// posisi sensor dari lantai
const float SENSOR_HEIGHT_CM = 149.50;

// batas pengukuran
const float MIN_HEIGHT_CM = 50.00;
const float MAX_HEIGHT_CM = 150.00;

// toleransi tinggi
const float HEIGHT_STABLE_TOLERANCE = 0.50;

// harus stabil selama 2 detik
const unsigned long HEIGHT_STABLE_TIME = 2000;

// =============================================================
// VARIABEL DATA BALITA
// =============================================================

String currentUID = "";

String childName = "";
String childSex  = "";

int childAgeMonth = 0;

bool childRegistered = false;

// =============================================================
// VARIABEL HASIL PEMERIKSAAN
// =============================================================

float finalWeight = 0.00;
float finalHeight = 0.00;

float finalZScore = 0.00;

String finalCategory = "";

// =============================================================
// STATE SISTEM
// =============================================================

enum SystemState
{
  STATE_STANDBY,
  STATE_CHECK_RFID,
  STATE_UNKNOWN_RFID,
  STATE_REGISTERED,
  STATE_WEIGHING,
  STATE_WEIGHT_LOCK,
  STATE_HEIGHT_MEASURING,
  STATE_RESULT_Z,
  STATE_RESULT_ALL,
  STATE_FINISH
};

SystemState currentState = STATE_STANDBY;

// =============================================================
// TIMER
// =============================================================

unsigned long stateStartTime = 0;

unsigned long stableWeightStart = 0;
unsigned long stableHeightStart = 0;

// =============================================================
// VARIABEL FILTER BERAT
// =============================================================

float previousWeight = 0.00;
float stableWeight = 0.00;

bool weightStable = false;

// =============================================================
// VARIABEL FILTER TINGGI
// =============================================================

float previousHeight = 0.00;
float stableHeight = 0.00;

bool heightStable = false;

// buffer median tinggi
const int HEIGHT_SAMPLE_COUNT = 7;
float heightSamples[HEIGHT_SAMPLE_COUNT];
int heightSampleIndex = 0;

// =============================================================
// LCD LOW LEVEL
// =============================================================

void lcdExpanderWrite(uint8_t data)
{
  I2C_LCD.beginTransmission(LCD_ADDRESS);
  I2C_LCD.write(data | LCD_BL);
  I2C_LCD.endTransmission();
}

void lcdPulseEnable(uint8_t data)
{
  lcdExpanderWrite(data | LCD_EN);
  delayMicroseconds(1);

  lcdExpanderWrite(data & ~LCD_EN);
  delayMicroseconds(50);
}

void lcdWrite4Bits(uint8_t value)
{
  uint8_t data = 0;

  if (value & 0x01) data |= 0x10;
  if (value & 0x02) data |= 0x20;
  if (value & 0x04) data |= 0x40;
  if (value & 0x08) data |= 0x80;

  lcdExpanderWrite(data);
  lcdPulseEnable(data);
}

void lcdSend(uint8_t value, uint8_t mode)
{
  uint8_t high = value >> 4;
  uint8_t low  = value & 0x0F;

  uint8_t highData = 0;
  uint8_t lowData  = 0;

  if (high & 0x01) highData |= 0x10;
  if (high & 0x02) highData |= 0x20;
  if (high & 0x04) highData |= 0x40;
  if (high & 0x08) highData |= 0x80;

  if (low & 0x01) lowData |= 0x10;
  if (low & 0x02) lowData |= 0x20;
  if (low & 0x04) lowData |= 0x40;
  if (low & 0x08) lowData |= 0x80;

  highData |= mode;
  lowData  |= mode;

  lcdPulseEnable(highData);
  lcdPulseEnable(lowData);
}

void lcdCommand(uint8_t command)
{
  lcdSend(command, 0);
}

void lcdWriteChar(char c)
{
  lcdSend(c, LCD_RS);
}

void lcdClear()
{
  lcdCommand(0x01);
  delay(2);
}

void lcdSetCursor(uint8_t col, uint8_t row)
{
  static const uint8_t rowOffsets[] =
  {
    0x00,
    0x40,
    0x14,
    0x54
  };

  lcdCommand(0x80 | (col + rowOffsets[row]));
}

void lcdPrint(String text)
{
  for (uint16_t i = 0; i < text.length(); i++)
  {
    lcdWriteChar(text[i]);
  }
}

void lcdPrintLine(uint8_t row, String text)
{
  lcdSetCursor(0, row);

  while (text.length() < 20)
    text += " ";

  if (text.length() > 20)
    text = text.substring(0, 20);

  lcdPrint(text);
}

void lcdBegin()
{
  delay(50);

  lcdWrite4Bits(0x03);
  delay(5);

  lcdWrite4Bits(0x03);
  delayMicroseconds(150);

  lcdWrite4Bits(0x03);
  lcdWrite4Bits(0x02);

  lcdCommand(0x28); // 4-bit, 2-line, 5x8
  lcdCommand(0x0C); // display ON
  lcdCommand(0x06); // cursor increment
  lcdClear();
}

// =============================================================
// BUZZER
// =============================================================

void buzzerOn(int frequency = 5000)
{
  ledcWriteTone(BUZZER_PIN, frequency);
}

void buzzerOff()
{
  ledcWriteTone(BUZZER_PIN, 0);
}

void beepShort()
{
  buzzerOn(5000);
  delay(100);
  buzzerOff();
}

void beepDouble()
{
  buzzerOn(5000);
  delay(100);
  buzzerOff();

  delay(100);

  buzzerOn(5000);
  delay(100);
  buzzerOff();
}

void beepLong()
{
  buzzerOn(5000);
  delay(500);
  buzzerOff();
}

void beepSuccess()
{
  buzzerOn(2800);
  delay(110);
  buzzerOff();
  delay(80);
  buzzerOn(4300);
  delay(180);
  buzzerOff();
}

void beepError()
{
  for (int i = 0; i < 3; i++)
  {
    buzzerOn(1200);
    delay(170);
    buzzerOff();
    if (i < 2) delay(100);
  }
}

// Peringatan khusus saat kartu yang ditempel pada mode pendaftaran
// ternyata sudah terhubung dengan balita lain.
void beepAlreadyRegistered()
{
  for (int i = 0; i < 2; i++)
  {
    buzzerOn(3600);
    delay(120);
    buzzerOn(1700);
    delay(220);
    buzzerOff();
    if (i == 0) delay(100);
  }
}

// Bunyi sangat singkat: kartu baru saja terbaca secara lokal.
void beepCardDetected()
{
  buzzerOn(4800);
  delay(65);
  buzzerOff();
}

// Dua nada rendah-menengah: data berat sudah stabil dan terkunci.
void beepWeightLocked()
{
  buzzerOn(1800);
  delay(130);
  buzzerOff();
  delay(75);
  buzzerOn(2600);
  delay(190);
  buzzerOff();
}

// Tiga nada naik: data tinggi sudah stabil dan terkunci.
void beepHeightLocked()
{
  const int notes[] = {2600, 3400, 4200};
  const int durations[] = {90, 110, 230};
  for (int i = 0; i < 3; i++)
  {
    buzzerOn(notes[i]);
    delay(durations[i]);
    buzzerOff();
    if (i < 2) delay(65);
  }
}

// Empat nada khas: hasil server sudah tampil dan pemeriksaan selesai.
void beepResultReady()
{
  const int notes[] = {2200, 3000, 3900, 5000};
  const int durations[] = {120, 120, 150, 350};
  for (int i = 0; i < 4; i++)
  {
    buzzerOn(notes[i]);
    delay(durations[i]);
    buzzerOff();
    if (i < 3) delay(70);
  }
}

// =============================================================
// WIFI
// =============================================================

void connectWiFi(bool forcePortal = false)
{
  // Instalasi ini hanya memakai satu alat. Identitas API ditentukan oleh
  // token, bukan lagi oleh ID chip ESP32 yang dapat berubah saat board diganti.
  deviceId = "SIP-STUNTING-01";

  String apName = "SIP-STUNTING-IOT";
  String apPassword = "SIPSTUNTING";
  char tokenBuffer[97];
  deviceToken.substring(0, 96).toCharArray(tokenBuffer, sizeof(tokenBuffer));

  // Token belum tersimpan (belum pernah dikonfigurasi) harus tetap membuka
  // portal walau WiFi sudah tersimpan/terhubung, karena field token hanya
  // ada di halaman portal ini.
  bool needsPortal = forcePortal || deviceToken.length() < 32;

  WiFiManager manager;
  WiFiManagerParameter tokenParam("token", "Token perangkat dari website", tokenBuffer, 96);
  manager.addParameter(&tokenParam);
  manager.setConfigPortalTimeout(180);
  manager.setConnectTimeout(20);

  lcdClear();
  lcdPrintLine(0, needsPortal ? "KONFIGURASI ALAT" : "MENGHUBUNGKAN WIFI");
  lcdPrintLine(1, deviceId);
  if (needsPortal || WiFi.SSID().isEmpty()) {
    lcdPrintLine(2, apName);
    lcdPrintLine(3, "Sandi: " + apPassword);
  } else {
    lcdPrintLine(2, "Mohon tunggu...");
  }

  bool connected = needsPortal
    ? manager.startConfigPortal(apName.c_str(), apPassword.c_str())
    : manager.autoConnect(apName.c_str(), apPassword.c_str());

  String submittedToken = String(tokenParam.getValue());
  submittedToken.trim();
  if (submittedToken.length() >= 32 && submittedToken != deviceToken) {
    deviceToken = submittedToken;
    preferences.putString("device_token", deviceToken);
  }

  if (connected) {
    // Matikan hemat daya WiFi: mode tidur modem menambah latensi tiap paket.
    WiFi.setSleep(false);
    configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com");
    time_t now = time(nullptr);
    unsigned long waitStarted = millis();
    while (now < 1700000000 && millis() - waitStarted < 10000) {
      delay(250);
      now = time(nullptr);
    }
  }

  lcdPrintLine(3, connected ? "WiFi Terhubung" : "WiFi Gagal");
  Serial.println(connected ? "WiFi TERHUBUNG" : "WiFi GAGAL");
  delay(1200);
}

// =============================================================
// RECONNECT WIFI
// =============================================================

bool ensureWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
    return true;

  WiFi.reconnect();
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) delay(250);
  return WiFi.status() == WL_CONNECTED;
}

int apiPost(String endpoint, JsonDocument &request, JsonDocument &response)
{
  if (!ensureWiFi() || deviceToken.length() < 32)
    return -1;

  if (!secureClientReady) {
    secureClient.setCACert(ROOT_CA);
    secureClientReady = true;
  }

  String url = String(API_BASE_URL) + "/" + endpoint;
  String body;
  serializeJson(request, body);

  int code = -1;
  String raw;

  // Percobaan kedua hanya bila koneksi lama yang dipakai ulang ternyata sudah
  // ditutup server. Aman diulang: heartbeat/lookup idempoten dan pengiriman
  // pemeriksaan memakai request_id.
  for (int attempt = 0; attempt < 2; attempt++)
  {
    bool reused = secureClient.connected();

    HTTPClient http;
    http.setReuse(true);
    http.setConnectTimeout(2500);
    http.setTimeout(2000);
    if (!http.begin(secureClient, url)) return -1;

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("Authorization", "Bearer " + deviceToken);

    code = http.POST(body);
    raw = code > 0 ? http.getString() : "";
    http.end();

    if (code > 0) break;

    secureClient.stop(); // koneksi rusak: buang dan buka yang baru
    if (!reused) break;  // sudah koneksi baru, tidak perlu diulang
  }

  if (code == 401 || code == 403) {
    authFailureCount++;
  } else if (code > 0) {
    authFailureCount = 0;
  }

  if (raw.length() > 0) {
    DeserializationError error = deserializeJson(response, raw);
    if (error) {
      Serial.println("JSON API tidak valid: " + raw);
      return -2;
    }
  }
  return code;
}

void sendHeartbeat()
{
  JsonDocument request;
  JsonDocument response;
  request["firmware_version"] = FIRMWARE_VERSION;
  request["wifi_rssi"] = WiFi.RSSI();
  request["uptime_seconds"] = millis() / 1000;
  JsonObject sensors = request["sensors"].to<JsonObject>();
  sensors["rfid"] = rfidReady;
  sensors["weight"] = scaleReady;
  sensors["height"] = tofReady;
  sensors["lcd"] = lcdReady;
  int code = apiPost("heartbeat", request, response);
  Serial.println("Heartbeat HTTP " + String(code));
  if (authFailureCount >= 3) {
    authFailureCount = 0;
    secureClient.stop(); // bebaskan memori TLS sebelum portal konfigurasi
    connectWiFi(true);
  }
}

// =============================================================
// URL ENCODING SEDERHANA
// =============================================================

String urlEncode(String value)
{
  String encoded = "";

  const char *hex = "0123456789ABCDEF";

  for (int i = 0; i < value.length(); i++)
  {
    char c = value[i];

    if (
      (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      c == '-' ||
      c == '_' ||
      c == '.' ||
      c == '~'
    )
    {
      encoded += c;
    }
    else
    {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }

  return encoded;
}

// =============================================================
// AMBIL VALUE JSON SEDERHANA
// =============================================================

String getJsonString(String json, String key)
{
  String search = "\"" + key + "\"";

  int p = json.indexOf(search);

  if (p < 0)
    return "";

  p = json.indexOf(':', p);

  if (p < 0)
    return "";

  p++;

  while (p < json.length() &&
         (json[p] == ' ' || json[p] == '\"'))
  {
    p++;
  }

  int end;

  if (json[p - 1] == '\"')
    end = json.indexOf('\"', p);
  else
    end = json.indexOf(',', p);

  if (end < 0)
    end = json.indexOf('}', p);

  if (end < 0)
    return "";

  return json.substring(p, end);
}

float getJsonFloat(String json, String key)
{
  String value = getJsonString(json, key);

  if (value == "")
    return 0.0;

  return value.toFloat();
}

int getJsonInt(String json, String key)
{
  String value = getJsonString(json, key);

  if (value == "")
    return 0;

  return value.toInt();
}

// =============================================================
// RFID UID
// =============================================================

String readRFIDUID()
{
  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++)
  {
    if (i > 0)
      uid += ":";

    if (rfid.uid.uidByte[i] < 0x10)
      uid += "0";

    uid += String(rfid.uid.uidByte[i], HEX);
  }

  uid.toUpperCase();

  return uid;
}

// =============================================================
// CEK RFID KE API
// =============================================================

RFIDCheckResult checkRFID(String uid)
{
  JsonDocument request;
  JsonDocument response;
  request["uid"] = uid;
  int code = apiPost("rfid/lookup", request, response);
  if (code <= 0) return RFID_API_ERROR;

  String status = response["status"] | "error";
  if (status == "captured") {
    childRegistered = false;
    String capturedFor = response["mode"] | "";
    bool alreadyRegistered = response["registered"] | false;
    if (capturedFor == "register" && alreadyRegistered)
      return RFID_CAPTURED_REGISTERED;
    return RFID_CAPTURED;
  }
  if (status == "registered") {
    childRegistered = true;
    childName = response["child"]["name"].as<String>();
    childSex = response["child"]["sex"].as<String>();
    childAgeMonth = response["child"]["age_days"].as<int>() / 30;
    return RFID_REGISTERED;
  }
  childRegistered = false;
  if (status == "unknown") return RFID_UNKNOWN;
  if (status == "ineligible" || status == "inactive") return RFID_INELIGIBLE;
  return RFID_API_ERROR;
}

// =============================================================
// TAMPILKAN DATA ANAK
// =============================================================

void showChildData()
{
  lcdClear();

  lcdPrintLine(0, "DATA DITEMUKAN");

  String nameLine = childName;

  if (nameLine.length() > 20)
    nameLine = nameLine.substring(0, 20);

  lcdPrintLine(1, nameLine);

  String info =
    childSex + " | " +
    String(childAgeMonth) +
    " bulan";

  lcdPrintLine(2, info);

  lcdPrintLine(3, "Menyiapkan alat...");

  delay(2500);
}

// =============================================================
// RESET BUFFER TINGGI
// =============================================================

void resetHeightBuffer()
{
  for (int i = 0; i < HEIGHT_SAMPLE_COUNT; i++)
  {
    heightSamples[i] = 0;
  }

  heightSampleIndex = 0;

  previousHeight = 0;
  stableHeight = 0;

  heightStable = false;

  stableHeightStart = 0;
}

// =============================================================
// HITUNG MEDIAN
// =============================================================

float calculateMedian(float values[], int count)
{
  float temp[HEIGHT_SAMPLE_COUNT];

  for (int i = 0; i < count; i++)
  {
    temp[i] = values[i];
  }

  for (int i = 0; i < count - 1; i++)
  {
    for (int j = i + 1; j < count; j++)
    {
      if (temp[j] < temp[i])
      {
        float t = temp[i];
        temp[i] = temp[j];
        temp[j] = t;
      }
    }
  }

  if (count % 2 == 1)
    return temp[count / 2];

  return (temp[count / 2 - 1] + temp[count / 2]) / 2.0;
}

// =============================================================
// BACA TINGGI
// =============================================================

float readHeight()
{
  uint16_t distance = tof.read();

  if (tof.timeoutOccurred())
  {
    return -1;
  }

  // Jarak sensor ke kepala
  float distanceCM = distance / 10.0;

  // Tinggi badan = posisi sensor - jarak
  float heightCM =
    SENSOR_HEIGHT_CM - distanceCM;

  if (
    heightCM < MIN_HEIGHT_CM ||
    heightCM > MAX_HEIGHT_CM
  )
  {
    return -1;
  }

  return heightCM;
}

// =============================================================
// FILTER TINGGI
// =============================================================

float getFilteredHeight()
{
  float h = readHeight();

  if (h <= 0)
    return -1;

  heightSamples[heightSampleIndex] = h;

  heightSampleIndex++;

  if (heightSampleIndex >= HEIGHT_SAMPLE_COUNT)
    heightSampleIndex = 0;

  int validCount = 0;

  float temp[HEIGHT_SAMPLE_COUNT];

  for (int i = 0; i < HEIGHT_SAMPLE_COUNT; i++)
  {
    if (heightSamples[i] > 0)
    {
      temp[validCount] = heightSamples[i];
      validCount++;
    }
  }

  if (validCount < 3)
    return h;

  return calculateMedian(temp, validCount);
}

// =============================================================
// BACA BERAT
// =============================================================

float readWeight()
{
  if (!scale.is_ready())
    return -1;

  float weight = scale.get_units(5);

  // Hilangkan nilai negatif kecil
  if (weight < 0 && weight > -0.20)
    weight = 0;

  if (weight < 0)
    weight = 0;

  // Resolusi 0.05 kg
  weight =
    round(weight / 0.05) * 0.05;

  // Zero kecil
  if (weight < 0.10)
    weight = 0.00;

  return weight;
}

// =============================================================
// TAMPILKAN BERAT
// =============================================================

void showWeight(float weight)
{
  lcdPrintLine(
    1,
    "Berat: " +
    String(weight, 2) +
    " kg"
  );

  lcdPrintLine(
    2,
    "Menunggu stabil..."
  );
}

// =============================================================
// PROSES BERAT
// =============================================================

bool processWeight()
{
  float weight = readWeight();

  if (weight < 0)
    return false;

  showWeight(weight);

  // Anak belum naik
  if (weight < MIN_WEIGHT)
  {
    stableWeightStart = 0;
    weightStable = false;

    previousWeight = weight;

    return false;
  }

  // Perbedaan dengan pembacaan sebelumnya
  float difference =
    abs(weight - previousWeight);

  previousWeight = weight;

  if (difference <= STABLE_TOLERANCE)
  {
    if (stableWeightStart == 0)
    {
      stableWeightStart = millis();
    }

    if (
      millis() - stableWeightStart >=
      STABLE_TIME
    )
    {
      stableWeight = weight;
      weightStable = true;

      return true;
    }
  }
  else
  {
    stableWeightStart = millis();
  }

  return false;
}

// =============================================================
// TAMPILKAN WEIGHT LOCK
// =============================================================

void showWeightLock()
{
  lcdClear();

  lcdPrintLine(0, "BERAT TERKUNCI");

  lcdPrintLine(
    1,
    "Berat: " +
    String(stableWeight, 2) +
    " kg"
  );

  lcdPrintLine(2, "Silakan berdiri");
  lcdPrintLine(3, "Mengukur tinggi...");

  beepWeightLocked();
}

// =============================================================
// PROSES TINGGI
// =============================================================

bool processHeight()
{
  float height = getFilteredHeight();

  if (height <= 0)
  {
    stableHeightStart = 0;

    lcdPrintLine(1, "Tinggi: --.-- cm");
    lcdPrintLine(2, "Posisi tidak sesuai");

    return false;
  }

  // PENTING:
  // height TIDAK dibulatkan.
  // Nilai asli ini akan dipakai untuk Z-Score.

  Serial.print("Tinggi presisi: ");
  Serial.print(height, 2);
  Serial.println(" cm");

  lcdPrintLine(
    1,
    "Tinggi: " +
    String(height, 2) +
    " cm"
  );

  lcdPrintLine(
    2,
    "Menunggu stabil..."
  );

  if (previousHeight == 0)
  {
    previousHeight = height;
    stableHeightStart = millis();

    return false;
  }

  float difference =
    abs(height - previousHeight);

  previousHeight = height;

  if (difference <= HEIGHT_STABLE_TOLERANCE)
  {
    if (stableHeightStart == 0)
      stableHeightStart = millis();

    if (
      millis() - stableHeightStart >=
      HEIGHT_STABLE_TIME
    )
    {
      stableHeight = height;
      heightStable = true;

      return true;
    }
  }
  else
  {
    stableHeightStart = millis();
  }

  return false;
}

// =============================================================
// KIRIM HASIL PEMERIKSAAN KE API
// =============================================================

bool submitMeasurement(String uid, float weight, float height, String requestId)
{
  JsonDocument request;
  JsonDocument response;
  request["request_id"] = requestId;
  request["uid"] = uid;
  request["weight_kg"] = serialized(String(weight, 2));
  request["height_cm"] = serialized(String(height, 2));
  int code = apiPost("measurements", request, response);
  if (code != 200 || String(response["status"] | "") != "success") return false;
  finalZScore = response["z_score"].as<float>();
  finalCategory = response["category"].as<String>();
  return true;
}

void savePendingMeasurement(String uid, float weight, float height, String requestId)
{
  pendingMeasurement = true;
  pendingUid = uid;
  pendingWeight = weight;
  pendingHeight = height;
  pendingRequestId = requestId;
  preferences.putBool("pending", true);
  preferences.putString("pending_uid", uid);
  preferences.putString("pending_req", requestId);
  preferences.putFloat("pending_w", weight);
  preferences.putFloat("pending_h", height);
}

void clearPendingMeasurement()
{
  pendingMeasurement = false;
  preferences.remove("pending");
  preferences.remove("pending_uid");
  preferences.remove("pending_req");
  preferences.remove("pending_w");
  preferences.remove("pending_h");
}

bool sendMeasurement()
{
  String requestId = deviceId + "-" + String(millis(), HEX) + "-" + String(esp_random(), HEX);
  if (submitMeasurement(currentUID, stableWeight, stableHeight, requestId)) return true;
  savePendingMeasurement(currentUID, stableWeight, stableHeight, requestId);
  return false;
}

// =============================================================
// HASIL Z-SCORE
// =============================================================

void showZScoreResult()
{
  lcdClear();

  lcdPrintLine(0, "HASIL TB/U");

  lcdPrintLine(
    1,
    "Z-Score: " +
    String(finalZScore, 2) +
    " SD"
  );

  lcdPrintLine(
    2,
    "Kategori:"
  );

  lcdPrintLine(
    3,
    finalCategory
  );

  // Hasil Z-Score tetap ditampilkan
  delay(5000);
}

// =============================================================
// HASIL KESELURUHAN
// =============================================================

void showAllResultPage1()
{
  lcdClear();

  lcdPrintLine(
    0,
    "HASIL PEMERIKSAAN"
  );

  lcdPrintLine(
    1,
    "Berat : " +
    String(stableWeight, 2) +
    " kg"
  );

  lcdPrintLine(
    2,
    "Tinggi: " +
    String(stableHeight, 2) +
    " cm"
  );

  lcdPrintLine(
    3,
    "Z     : " +
    String(finalZScore, 2) +
    " SD"
  );

  delay(5000);
}

void showAllResultPage2()
{
  lcdClear();

  lcdPrintLine(
    0,
    "HASIL TB/U"
  );

  lcdPrintLine(
    1,
    "Z-Score: " +
    String(finalZScore, 2) +
    " SD"
  );

  lcdPrintLine(
    2,
    "Kategori:"
  );

  lcdPrintLine(
    3,
    finalCategory
  );

  delay(5000);
}

// =============================================================
// PEMERIKSAAN SELESAI
// =============================================================

void showFinish()
{
  lcdClear();

  lcdPrintLine(0, "PEMERIKSAAN");
  lcdPrintLine(1, "SELESAI");
  lcdPrintLine(2, "");
  lcdPrintLine(3, "Silakan turun...");

  beepResultReady();

  // Jangan langsung kembali standby.
  // Sistem menunggu anak turun dari timbangan.
}

// =============================================================
// CEK ANAK SUDAH TURUN
// =============================================================

bool isWeightEmpty()
{
  float weight = readWeight();

  if (weight < 0)
    return false;

  return weight < MIN_WEIGHT;
}

// =============================================================
// STANDBY LCD
// =============================================================

void showCaptureRequest()
{
  lcdClear();

  if (captureMode == "register") {
    lcdPrintLine(0, "MODE PENDAFTARAN");
    lcdPrintLine(1, "Tempelkan kartu");
    lcdPrintLine(2, "balita yang akan");
    lcdPrintLine(3, "didaftarkan di web");
  } else {
    lcdPrintLine(0, "PILIH BALITA");
    lcdPrintLine(1, "Tempelkan kartu");
    lcdPrintLine(2, "untuk pemeriksaan");
    lcdPrintLine(3, "(permintaan web)");
  }
}

void showStandby()
{
  if (captureMode != "off") {
    showCaptureRequest();
    return;
  }

  lcdClear();

  lcdPrintLine(0, "SISTEM IoT");
  lcdPrintLine(1, "DETEKSI STUNTING");
  lcdPrintLine(2, "Tempelkan RFID");
  lcdPrintLine(3, "");
}

// Tanya server apakah website sedang meminta kartu. Hanya dipanggil saat
// STANDBY dan tidak ada kartu di reader. Bila gagal, jeda diperpanjang agar
// pembacaan RFID tidak tersendat oleh koneksi yang buruk.
void pollCaptureMode()
{
  if (millis() - lastModePollAt < modePollInterval) return;
  lastModePollAt = millis();
  if (WiFi.status() != WL_CONNECTED || deviceToken.length() < 32) return;

  JsonDocument request;
  JsonDocument response;
  request["ping"] = 1; // body harus objek JSON; dokumen kosong terkirim sebagai "null"
  int code = apiPost("mode", request, response);
  if (code != 200) {
    modePollInterval = 5000;
    return;
  }
  modePollInterval = 1000;

  String newMode = response["mode"] | "off";
  if (newMode != "register" && newMode != "select") newMode = "off";
  if (newMode != captureMode) {
    captureMode = newMode;
    Serial.println("Mode tangkap kartu: " + captureMode);
    showStandby();
  }
}

// Kartu berhasil dikirim ke website (mode tangkap).
void showCaptureDone()
{
  lcdClear();

  lcdPrintLine(0, "KARTU TERBACA");
  lcdPrintLine(1, currentUID);
  lcdPrintLine(2, "Dikirim ke website");
  lcdPrintLine(3, "Lanjutkan di web");

  beepSuccess();
  delay(1200);
}

void showCaptureAlreadyRegistered()
{
  lcdClear();
  lcdPrintLine(0, "KARTU SUDAH");
  lcdPrintLine(1, "TERDAFTAR");
  lcdPrintLine(2, currentUID);
  lcdPrintLine(3, "Pilih kartu lain");

  beepAlreadyRegistered();
  delay(3000);
}

// Umpan balik instan begitu UID terbaca, SEBELUM menunggu jaringan/server,
// supaya pengguna langsung tahu kartunya sudah terbaca.
void showCardDetected()
{
  lcdPrintLine(0, "KARTU TERBACA");
  lcdPrintLine(1, currentUID);
  lcdPrintLine(2, captureMode != "off" ? "Mengirim ke website" : "Memeriksa data...");
  lcdPrintLine(3, "");
  beepShort();
}

// =============================================================
// RFID BELUM TERDAFTAR
// =============================================================

void showUnknownRFID()
{
  lcdClear();

  lcdPrintLine(0, "RFID TERDETEKSI");

  lcdPrintLine(
    1,
    "UID:"
  );

  lcdPrintLine(
    2,
    currentUID
  );

  lcdPrintLine(
    3,
    "Belum Terdaftar"
  );

  beepError();

  delay(1500);

  lcdClear();

  lcdPrintLine(0, "KARTU BELUM");
  lcdPrintLine(1, "TERDAFTAR");
  lcdPrintLine(2, "Buka Website");
  lcdPrintLine(3, "Menu Pendaftaran");

  delay(1800);
}

// =============================================================
// RFID TERDAFTAR
// =============================================================

void showRegisteredRFID()
{
  lcdClear();

  lcdPrintLine(
    0,
    "RFID TERDETEKSI"
  );

  lcdPrintLine(
    1,
    childName
  );

  lcdPrintLine(
    2,
    "Umur: " +
    String(childAgeMonth) +
    " bulan"
  );

  lcdPrintLine(
    3,
    "Data ditemukan"
  );

  beepSuccess();

  delay(1200);
}

// =============================================================
// INISIALISASI TOF
// =============================================================

bool initToF()
{
  Wire.begin(TOF_SDA, TOF_SCL);

  tof.setTimeout(500);

  if (!tof.init())
  {
    tofReady = false;
    Serial.println("VL53L1X gagal");

    return false;
  }

  // Mode jarak panjang
  tof.setDistanceMode(
    VL53L1X::Long
  );

  // 50 ms
  tof.setMeasurementTimingBudget(
    50000
  );

  tof.startContinuous(50);

  Serial.println("VL53L1X siap");

  tofReady = true;

  return true;
}

// =============================================================
// INISIALISASI HX711
// =============================================================

void initHX711()
{
  scale.begin(
    HX711_DT,
    HX711_SCK
  );

  scale.set_scale(
    CALIBRATION_FACTOR
  );

  Serial.println(
    "HX711 siap"
  );

  Serial.print(
    "Calibration Factor: "
  );

  Serial.println(
    CALIBRATION_FACTOR
  );

  // Tare
  lcdClear();

  lcdPrintLine(
    0,
    "INISIALISASI"
  );

  lcdPrintLine(
    1,
    "Tare timbangan..."
  );

  delay(1000);

  scale.tare(20);

  Serial.println(
    "Tare selesai"
  );

  scaleReady = scale.is_ready();

  delay(1000);
}

// =============================================================
// INISIALISASI DAN PEMULIHAN RFID RC522
// =============================================================

bool isRFIDVersionValid(byte version)
{
  return version == 0x12 || // chip counterfeit yang umum
         version == 0x82 || // clone RC522 pada alat ini (teruji membaca UID)
         version == 0x88 || // clone FM17522
         version == 0x90 || // MFRC522 v0.0
         version == 0x91 || // MFRC522 v1.0
         version == 0x92;   // MFRC522 v2.0
}

bool initRFID()
{
  // Pastikan chip-select tidak aktif ketika bus SPI mulai. Beberapa modul
  // RC522 tidak merespons setelah boot bila SS/RST sempat mengambang.
  pinMode(RFID_SS, OUTPUT);
  digitalWrite(RFID_SS, HIGH);
  pinMode(RFID_RST, OUTPUT);
  digitalWrite(RFID_RST, LOW);
  delay(10);
  digitalWrite(RFID_RST, HIGH);
  delay(100);

  SPI.begin(
    RFID_SCK,
    RFID_MISO,
    RFID_MOSI,
    RFID_SS
  );

  rfid.PCD_Init();
  delay(50);

  byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.printf("RFID RC522 VersionReg: 0x%02X\n", version);

  // Hanya nilai versi RC522 yang dikenal boleh dianggap siap. Nilai acak
  // seperti 0xFE juga menandakan MISO/SS mengambang atau komunikasi gagal.
  if (!isRFIDVersionValid(version))
  {
    rfidReady = false;
    Serial.println("RFID RC522 tidak terdeteksi - periksa 3.3V dan kabel SPI");
    return false;
  }

  // Maksimalkan sensitivitas agar kartu lebih mudah terbaca pada casing alat.
  rfid.PCD_SetAntennaGain(MFRC522::RxGain_max);
  rfid.PCD_AntennaOn();
  lastRFIDHealthCheckAt = millis();
  rfidReady = true;

  Serial.println("RFID RC522 siap");
  return true;
}

void recoverRFIDIfNeeded()
{
  if (millis() - lastRFIDHealthCheckAt < RFID_HEALTH_CHECK_INTERVAL)
    return;

  lastRFIDHealthCheckAt = millis();
  byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  if (!isRFIDVersionValid(version))
  {
    Serial.println("Komunikasi RFID terputus, mencoba inisialisasi ulang...");
    rfidReady = initRFID();
  }
  else
  {
    rfidReady = true;
  }
}

bool isLCDReady()
{
  I2C_LCD.beginTransmission(LCD_ADDRESS);
  return I2C_LCD.endTransmission() == 0;
}

// =============================================================
// SETUP
// =============================================================

void setup()
{
  Serial.begin(115200);

  preferences.begin("sip-stunting", false);
  deviceToken = preferences.getString("device_token", "");
  pendingMeasurement = preferences.getBool("pending", false);
  if (pendingMeasurement) {
    pendingUid = preferences.getString("pending_uid", "");
    pendingRequestId = preferences.getString("pending_req", "");
    pendingWeight = preferences.getFloat("pending_w", 0);
    pendingHeight = preferences.getFloat("pending_h", 0);
  }

  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" SISTEM IoT DETEKSI DINI STUNTING");
  Serial.println("========================================");

  // ---------------- LCD ----------------

  I2C_LCD.begin(
    LCD_SDA,
    LCD_SCL,
    400000
  );

  lcdBegin();
  lcdReady = isLCDReady();

  lcdClear();

  lcdPrintLine(0, "SISTEM IoT");
  lcdPrintLine(1, "DETEKSI STUNTING");
  lcdPrintLine(2, "Inisialisasi...");
  lcdPrintLine(3, "Mohon tunggu");

  // ---------------- BUZZER ----------------

  ledcAttach(
    BUZZER_PIN,
    5000,
    8
  );

  buzzerOff();

  // ---------------- HX711 ----------------

  initHX711();

  // ---------------- TOF ----------------

  if (!initToF())
  {
    lcdClear();

    lcdPrintLine(
      0,
      "ERROR VL53L1X"
    );

    lcdPrintLine(
      1,
      "Periksa kabel"
    );

    while (true)
    {
      beepError();
      delay(2000);
    }
  }

  // ---------------- RFID ----------------

  rfidReady = initRFID();

  // ---------------- WIFI ----------------

  connectWiFi();

  // ---------------- STANDBY ----------------

  showStandby();

  Serial.println();
  Serial.println(
    "SISTEM SIAP"
  );

  if (rfidReady && scaleReady && tofReady && lcdReady)
    beepSuccess();
  else
    beepError();
}

// =============================================================
// LOOP
// =============================================================

void loop()
{
  if (millis() - lastHeartbeatAt >= 10000 || lastHeartbeatAt == 0) {
    lastHeartbeatAt = millis();
    scaleReady = scale.is_ready();
    lcdReady = isLCDReady();
    if (!pendingMeasurement) sendHeartbeat();
  }

  if (pendingMeasurement) {
    lcdPrintLine(0, "DATA BELUM TERKIRIM");
    lcdPrintLine(1, "Menunggu internet...");
    lcdPrintLine(2, pendingUid);
    lcdPrintLine(3, "Jangan ukur dahulu");
    if (millis() - lastPendingRetryAt >= 5000 || lastPendingRetryAt == 0) {
      lastPendingRetryAt = millis();
      if (submitMeasurement(pendingUid, pendingWeight, pendingHeight, pendingRequestId)) {
        clearPendingMeasurement();
        lcdClear();
        lcdPrintLine(0, "DATA TERSINKRON");
        lcdPrintLine(1, "Z: " + String(finalZScore, 2));
        lcdPrintLine(2, finalCategory);
        delay(3000);
        showStandby();
      }
    }
    return;
  }

  // ===========================================================
  // STANDBY
  // ===========================================================

  if (currentState == STATE_STANDBY)
  {
    // Tunggu kartu RFID

    recoverRFIDIfNeeded();

    if (
      !rfid.PICC_IsNewCardPresent()
    )
    {
      // Tidak ada kartu: sekalian cek apakah website meminta kartu.
      pollCaptureMode();
      return;
    }

    if (
      !rfid.PICC_ReadCardSerial()
    )
    {
      // Gangguan singkat pada kartu/bus tidak perlu menggagalkan tap pertama.
      delay(5);
      if (!rfid.PICC_ReadCardSerial())
        return;
    }

    currentUID =
      readRFIDUID();

    Serial.println();
    Serial.println(
      "================================"
    );

    Serial.println(
      "RFID TERBACA"
    );

    Serial.println(
      "UID: " +
      currentUID
    );

    Serial.println(
      "================================"
    );

    // Umpan balik lokal langsung, tanpa menunggu respons HTTPS.
    beepCardDetected();

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

    // Beri tahu pengguna sekarang juga; pengecekan ke server menyusul.
    showCardDetected();

    currentState =
      STATE_CHECK_RFID;

    return;
  }

  // ===========================================================
  // CEK RFID
  // ===========================================================

  if (currentState == STATE_CHECK_RFID)
  {
    RFIDCheckResult result =
      checkRFID(currentUID);

    if (result == RFID_CAPTURED_REGISTERED) {
      showCaptureAlreadyRegistered();
      captureMode = "off";
      lastModePollAt = millis();
      currentUID = "";
      currentState = STATE_STANDBY;
      showStandby();
      return;
    }

    if (result == RFID_CAPTURED) {
      // Website meminta kartu (daftar/pilih balita): UID sudah dikirim,
      // jangan mulai penimbangan.
      showCaptureDone();
      captureMode = "off";
      lastModePollAt = millis();
      currentUID = "";
      currentState = STATE_STANDBY;
      showStandby();
      return;
    }
    if (result == RFID_REGISTERED) {
      currentState = STATE_REGISTERED;
      return;
    }
    if (result == RFID_UNKNOWN) {
      currentState = STATE_UNKNOWN_RFID;
      return;
    }
    lcdClear();
    if (result == RFID_INELIGIBLE) {
      lcdPrintLine(0, "TIDAK DAPAT DIUKUR");
      lcdPrintLine(1, "Usia harus 24-60");
      lcdPrintLine(2, "bulan / data nonaktif");
    } else {
      lcdPrintLine(0, "GANGGUAN API");
      lcdPrintLine(1, "RFID tidak diproses");
      lcdPrintLine(2, "Coba kembali");
    }
    beepError();
    delay(3000);
    currentUID = "";
    currentState = STATE_STANDBY;
    showStandby();
    return;
  }

  // ===========================================================
  // RFID BELUM TERDAFTAR
  // ===========================================================

  if (currentState == STATE_UNKNOWN_RFID)
  {
    showUnknownRFID();

    currentUID = "";

    currentState =
      STATE_STANDBY;

    showStandby();

    return;
  }

  // ===========================================================
  // RFID TERDAFTAR
  // ===========================================================

  if (currentState == STATE_REGISTERED)
  {
    showRegisteredRFID();

    lcdClear();

    lcdPrintLine(
      0,
      "SILAKAN NAIK"
    );

    lcdPrintLine(
      1,
      "Berat: --.-- kg"
    );

    lcdPrintLine(
      2,
      "Tunggu stabil..."
    );

    lcdPrintLine(
      3,
      "Jangan bergerak"
    );

    previousWeight = 0;
    stableWeightStart = 0;
    weightStable = false;

    currentState =
      STATE_WEIGHING;

    return;
  }

  // ===========================================================
  // MENIMBANG
  // ===========================================================

  if (currentState == STATE_WEIGHING)
  {
    if (processWeight())
    {
      finalWeight =
        stableWeight;

      currentState =
        STATE_WEIGHT_LOCK;

      stateStartTime =
        millis();
    }

    return;
  }

  // ===========================================================
  // BERAT LOCK
  // ===========================================================

  if (currentState == STATE_WEIGHT_LOCK)
  {
    showWeightLock();

    resetHeightBuffer();

    lcdClear();

    lcdPrintLine(
      0,
      "PENGUKURAN TINGGI"
    );

    lcdPrintLine(
      1,
      "Tinggi: --.-- cm"
    );

    lcdPrintLine(
      2,
      "Berdiri tegak"
    );

    lcdPrintLine(
      3,
      "Menunggu stabil..."
    );

    delay(500);

    currentState =
      STATE_HEIGHT_MEASURING;

    return;
  }

  // ===========================================================
  // PENGUKURAN TINGGI
  // ===========================================================

  if (
    currentState ==
    STATE_HEIGHT_MEASURING
  )
  {
    if (processHeight())
    {
      finalHeight =
        stableHeight;

      Serial.println();
      Serial.println(
        "TINGGI TERKUNCI"
      );

      Serial.print(
        "Tinggi presisi: "
      );

      Serial.print(
        finalHeight,
        2
      );

      Serial.println(
        " cm"
      );

      beepHeightLocked();

      lcdClear();

      lcdPrintLine(
        0,
        "TINGGI TERKUNCI"
      );

      lcdPrintLine(
        1,
        "Tinggi: " +
        String(finalHeight, 2) +
        " cm"
      );

      lcdPrintLine(
        2,
        "Menghitung TB/U"
      );

      lcdPrintLine(
        3,
        "Mohon tunggu..."

      );

      delay(1500);

      // Kirim ke API
      bool success =
        sendMeasurement();

      if (!success)
      {
        lcdClear();

        lcdPrintLine(
          0,
          "GAGAL KIRIM DATA"
        );

        beepError();

        lcdPrintLine(
          1,
          "Periksa WiFi/API"
        );

        delay(3000);

        currentState =
          STATE_STANDBY;

        showStandby();

        return;
      }

      currentState =
        STATE_RESULT_Z;
    }

    return;
  }

  // ===========================================================
  // HASIL Z-SCORE
  // ===========================================================

  if (
    currentState ==
    STATE_RESULT_Z
  )
  {
    showZScoreResult();

    currentState =
      STATE_RESULT_ALL;

    return;
  }

  // ===========================================================
  // HASIL KESELURUHAN
  // ===========================================================

  if (
    currentState ==
    STATE_RESULT_ALL
  )
  {
    showAllResultPage1();

    delay(500);

    showAllResultPage2();

    currentState =
      STATE_FINISH;

    showFinish();

    return;
  }

  // ===========================================================
  // SELESAI
  // ===========================================================

  if (
    currentState ==
    STATE_FINISH
  )
  {
    // Tunggu sampai anak turun
    // dari timbangan.

    if (isWeightEmpty())
    {
      delay(1000);

      // Pastikan benar-benar kosong
      if (isWeightEmpty())
      {
        Serial.println(
          "Anak sudah turun"
        );

        currentUID = "";

        childName = "";
        childSex = "";
        childAgeMonth = 0;

        finalWeight = 0;
        finalHeight = 0;
        finalZScore = 0;

        finalCategory = "";

        previousWeight = 0;
        previousHeight = 0;

        stableWeight = 0;
        stableHeight = 0;

        stableWeightStart = 0;
        stableHeightStart = 0;

        weightStable = false;
        heightStable = false;

        currentState =
          STATE_STANDBY;

        showStandby();
      }
    }

    return;
  }
}
