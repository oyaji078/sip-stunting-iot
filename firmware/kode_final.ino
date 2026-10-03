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

 Berat dan tinggi TIDAK dibulatkan. Nilai yang tampil di LCD
 pada saat terkunci adalah nilai yang sama persis dengan yang
 dikirim ke API (2 angka desimal, sama dengan kolom database).

 Contoh:
 Sensor membaca 88.60 cm

 Yang dikirim ke API:
 88.60 cm

 BUKAN:
 89 cm

 PHP/MySQL menggunakan tinggi presisi tersebut
 untuk menghitung Z-Score WHO.

===============================================================
 ALUR KERJA (alat merespons lebih dulu, web menyusul)
===============================================================

 1. Kartu ditempel -> buzzer + layar timbang SEGERA (tanpa menunggu web).
 2. Pengecekan kartu ke web berjalan di latar belakang (Core 0).
    - Kartu terdaftar    : nama dan umur tampil di LCD saat menimbang.
    - Kartu tidak dikenal / usia tidak sesuai / mode tangkap web:
      pengukuran dibatalkan dan pesan ditampilkan.
    - Web lambat / offline: alat tetap mengukur; web memvalidasi
      saat hasil dikirim (jika gagal kirim, data disimpan di NVS).
 3. Setelah berat dan tinggi terkunci, hasil dikirim ke web.
 4. Satu kartu hanya memicu satu pemeriksaan. Kartu harus dilepas
    dari pembaca sebelum kartu yang sama dapat dibaca lagi.

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
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <driver/gpio.h>
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
SemaphoreHandle_t apiMutex = nullptr;
unsigned long lastHeartbeatAt = 0;
unsigned long lastSensorCheckAt = 0;
unsigned long lastPendingRetryAt = 0;
unsigned long lastReconnectKickAt = 0;
bool pendingMeasurement = false;
String pendingUid;
String pendingRequestId;
float pendingWeight = 0;
float pendingHeight = 0;
bool rfidReady = false;
bool scaleReady = false;
bool tofReady = false;
bool lcdReady = false;

// true setelah setup() selesai; task RFID baru boleh membaca kartu sesudahnya.
volatile bool systemReady = false;

// true selama portal konfigurasi WiFi terbuka: task jaringan dan task RFID
// berhenti agar tidak mengganggu WiFiManager.
volatile bool portalActive = false;

// Task jaringan meminta portal dibuka (token ditolak berulang kali); loop()
// yang membukanya saat STANDBY karena portal memakai LCD.
volatile bool portalRequested = false;

// Mode tangkap kartu dari website. Saat aktif, kartu yang ditempel hanya
// dikirim ke web dan TIDAK memulai penimbangan. Ditulis oleh task jaringan
// (Core 0) dan dibaca loop() (Core 1), sehingga dipakai kode angka, bukan String.
enum CaptureMode : uint8_t
{
  CAPTURE_OFF = 0,
  CAPTURE_REGISTER = 1,
  CAPTURE_SELECT = 2
};
volatile uint8_t captureModeCode = CAPTURE_OFF;
volatile bool captureModeChanged = false;
unsigned long lastModePollAt = 0;
unsigned long modePollInterval = 1000;
unsigned long lastRFIDHealthCheckAt = 0;
const unsigned long RFID_HEALTH_CHECK_INTERVAL = 5000;

// Kartu harus lepas dari pembaca selama ini sebelum boleh dibaca lagi.
const unsigned long CARD_REMOVE_CONFIRM_MS = 800;

// Batas menunggu jawaban web ketika mode tangkap aktif (alat tidak menimbang).
const unsigned long LOOKUP_WAIT_TIMEOUT_MS = 15000;

enum RFIDCheckResult
{
  RFID_REGISTERED,
  RFID_UNKNOWN,
  RFID_INELIGIBLE,
  RFID_CAPTURED,
  RFID_CAPTURED_REGISTERED,
  RFID_API_ERROR
};

// Satu kartu = satu pemeriksaan. Setiap kartu yang diterima mendapat nomor
// urut (seq); hanya kartu dengan seq aktif yang hasilnya boleh dipakai. Hasil
// lama/basi (misalnya dari kartu yang sudah selesai diproses) otomatis dibuang,
// sehingga alat tidak pernah menghitung dua kali tanpa kartu ditempel.
struct LookupJob
{
  char uid[40];
  uint32_t seq;
};

struct RFIDResultData
{
  RFIDCheckResult result;
  uint32_t seq;
  char uid[40];
  char childName[64];
  char childSex[8];
  int childAgeMonth;
};

// Hasil pengiriman pemeriksaan ke web.
//  SUBMIT_OK       : web menyimpan data dan mengembalikan Z-Score.
//  SUBMIT_RETRY    : gagal sementara (WiFi/jaringan/server); data disimpan
//                    di NVS dan dikirim ulang dengan request_id yang sama.
//  SUBMIT_REJECTED : web menolak data (kartu tidak terdaftar/usia tidak sesuai/
//                    nilai di luar rentang). Dikirim ulang tidak akan berhasil,
//                    jadi TIDAK disimpan sebagai tertunda.
enum SubmitResult
{
  SUBMIT_OK,
  SUBMIT_RETRY,
  SUBMIT_REJECTED
};

String submitRejectReason;

uint32_t cardSeqCounter = 0;
volatile uint32_t activeCardSeq = 0; // 0 = tidak ada kartu yang sedang diproses

// taskRFID -> taskUpload: kartu yang perlu dicek ke web (kapasitas 1).
// taskRFID -> loop(): kartu baru terbaca, mulai pemeriksaan (kapasitas 1).
// taskUpload -> loop(): hasil pengecekan web.
QueueHandle_t lookupJobQueue = nullptr;
QueueHandle_t cardEventQueue = nullptr;
QueueHandle_t rfidResultQueue = nullptr;
TaskHandle_t taskRFIDHandle = nullptr;
TaskHandle_t taskUploadHandle = nullptr;

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
  STATE_AWAIT_LOOKUP, // hanya saat mode tangkap web aktif: tunggu jawaban web
  STATE_WEIGHING,
  STATE_WEIGHT_LOCK,
  STATE_HEIGHT_MEASURING,
  STATE_RESULT_Z,
  STATE_RESULT_ALL,
  STATE_FINISH
};

volatile SystemState currentState = STATE_STANDBY;

// =============================================================
// TIMER
// =============================================================

unsigned long stateStartTime = 0;
unsigned long awaitLookupStartedAt = 0;

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

// Frekuensi resonansi buzzer pasif umumnya 2.5-3 kHz: di sana suaranya paling
// keras. Ubah nilai ini bila buzzer yang dipakai punya resonansi berbeda.
const int BUZZER_LOUD_HZ = 2800;

// Bunyi PERTAMA setelah alat menyala dan semua sensor siap dipakai.
// Tiga nada naik, panjang, di frekuensi resonansi agar terdengar jelas.
void beepDeviceReady()
{
  const int notes[] = {2500, 2800, 3100};
  const int durations[] = {250, 250, 600};
  for (int i = 0; i < 3; i++)
  {
    buzzerOn(notes[i]);
    delay(durations[i]);
    buzzerOff();
    if (i < 2) delay(90);
  }
}

// Bunyi saat seluruh perhitungan selesai dan hasil dari web sudah diterima.
// Tiga bunyi panjang bernada sama; berbeda jelas dari bunyi siap (nada naik).
void beepResultReady()
{
  for (int i = 0; i < 3; i++)
  {
    buzzerOn(BUZZER_LOUD_HZ);
    delay(i < 2 ? 400 : 800);
    buzzerOff();
    if (i < 2) delay(120);
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
  portalActive = true; // jeda task jaringan dan pembaca RFID selama konfigurasi

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
  portalActive = false;
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
  if (deviceToken.length() < 32)
    return -1;

  // FIX: non-blocking - semua HTTPS berjalan di luar task RFID. Mutex ini
  // mencegah heartbeat, mode web, dan upload memakai TLS client bersamaan.
  if (apiMutex && xSemaphoreTake(apiMutex, portMAX_DELAY) != pdTRUE)
    return -1;

  if (!ensureWiFi()) {
    if (apiMutex) xSemaphoreGive(apiMutex);
    return -1;
  }

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
    if (!http.begin(secureClient, url)) {
      if (apiMutex) xSemaphoreGive(apiMutex);
      return -1;
    }

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

  if (apiMutex) xSemaphoreGive(apiMutex);

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
    // Portal memakai LCD, jadi loop() yang membukanya (saat STANDBY).
    authFailureCount = 0;
    portalRequested = true;
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

RFIDCheckResult checkRFID(
  const String &uid,
  String &resultName,
  String &resultSex,
  int &resultAgeMonth
)
{
  resultName = "";
  resultSex = "";
  resultAgeMonth = 0;

  JsonDocument request;
  JsonDocument response;
  request["uid"] = uid;
  int code = apiPost("rfid/lookup", request, response);
  if (code <= 0) return RFID_API_ERROR;

  String status = response["status"] | "error";
  if (status == "captured") {
    String capturedFor = response["mode"] | "";
    bool alreadyRegistered = response["registered"] | false;
    if (capturedFor == "register" && alreadyRegistered)
      return RFID_CAPTURED_REGISTERED;
    return RFID_CAPTURED;
  }
  if (status == "registered") {
    resultName = response["child"]["name"].as<String>();
    resultSex = response["child"]["sex"].as<String>();
    // Umur dalam bulan dihitung server (bulan kalender, sama dengan tampilan
    // web). age_days / 30.4375 hanya cadangan bila server versi lama.
    JsonObject child = response["child"];
    if (child["age_months"].is<int>())
      resultAgeMonth = child["age_months"].as<int>();
    else
      resultAgeMonth = (int)(child["age_days"].as<int>() / 30.4375f);
    return RFID_REGISTERED;
  }
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

  // Nilai negatif adalah drift nol timbangan, bukan berat.
  if (weight < 0)
    weight = 0;

  // Berat TIDAK dibulatkan. Nilai yang terkunci adalah pembacaan terakhir
  // apa adanya (rata-rata 5 sampel HX711). Hanya noise di bawah 0.10 kg yang
  // dianggap nol agar timbangan kosong terdeteksi dengan benar.
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

SubmitResult submitMeasurement(String uid, float weight, float height, String requestId)
{
  JsonDocument request;
  JsonDocument response;
  request["request_id"] = requestId;
  request["uid"] = uid;
  // Dikirim 2 desimal apa adanya (sama dengan yang tampil di LCD dan kolom
  // database); tidak ada pembulatan ke bilangan bulat atau 0.05.
  request["weight_kg"] = serialized(String(weight, 2));
  request["height_cm"] = serialized(String(height, 2));
  int code = apiPost("measurements", request, response);

  if (code == 200 && String(response["status"] | "") == "success") {
    finalZScore = response["z_score"].as<float>();
    finalCategory = response["category"].as<String>();
    return SUBMIT_OK;
  }

  if (code == 400 || code == 404 || code == 422) {
    submitRejectReason = response["error"]["message"] | "Data ditolak web";
    return SUBMIT_REJECTED;
  }

  return SUBMIT_RETRY;
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

SubmitResult sendMeasurement()
{
  String requestId = deviceId + "-" + String(millis(), HEX) + "-" + String(esp_random(), HEX);
  SubmitResult result = submitMeasurement(currentUID, stableWeight, stableHeight, requestId);
  if (result == SUBMIT_RETRY)
    savePendingMeasurement(currentUID, stableWeight, stableHeight, requestId);
  return result;
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

  // Bunyi selesai dibunyikan lebih awal, tepat saat hasil dari web diterima.

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

  if (captureModeCode == CAPTURE_REGISTER) {
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
  if (captureModeCode != CAPTURE_OFF) {
    showCaptureRequest();
    return;
  }

  lcdClear();

  lcdPrintLine(0, "SISTEM IoT");
  lcdPrintLine(1, "DETEKSI STUNTING");
  lcdPrintLine(2, "Tempelkan RFID");
  lcdPrintLine(3, "");
}

// Tanya server apakah website sedang meminta kartu. Dijalankan task jaringan
// (Core 0) hanya saat STANDBY dan tidak ada kartu yang sedang diproses, jadi
// tidak pernah menahan pembacaan RFID maupun penimbangan. Bila gagal, jeda
// diperpanjang agar koneksi yang buruk tidak membebani alat.
void pollCaptureModeBackground()
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
  uint8_t newCode = CAPTURE_OFF;
  if (newMode == "register") newCode = CAPTURE_REGISTER;
  else if (newMode == "select") newCode = CAPTURE_SELECT;

  // Kartu baru ditempel selagi permintaan ini berjalan: biarkan loop() yang
  // memutuskan; jangan ubah layar di bawah alur pemeriksaan.
  if (currentState != STATE_STANDBY || activeCardSeq != 0) return;

  if (newCode != captureModeCode) {
    captureModeCode = newCode;
    captureModeChanged = true; // loop() yang menggambar ulang LCD
    Serial.printf("Mode tangkap kartu: %u\n", (unsigned)newCode);
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

// Kartu sudah terbaca di alat; menunggu jawaban web (hanya saat mode tangkap).
void showAwaitLookup()
{
  lcdClear();
  lcdPrintLine(0, "KARTU TERBACA");
  lcdPrintLine(1, currentUID);
  lcdPrintLine(2, "Mengirim ke web...");
  lcdPrintLine(3, "");
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

void showIneligibleRFID()
{
  lcdClear();
  lcdPrintLine(0, "TIDAK DAPAT DIUKUR");
  lcdPrintLine(1, "Usia harus 24-60");
  lcdPrintLine(2, "bulan / data nonaktif");
  beepError();
  delay(3000);
}

void showApiTrouble()
{
  lcdClear();
  lcdPrintLine(0, "GANGGUAN API");
  lcdPrintLine(1, "RFID tidak diproses");
  lcdPrintLine(2, "Coba kembali");
  beepError();
  delay(3000);
}

// Cetak teks panjang membungkus ke beberapa baris LCD (20 karakter per baris).
void lcdPrintWrapped(uint8_t firstRow, String text)
{
  for (uint8_t row = firstRow; row < 4; row++)
  {
    lcdPrintLine(row, text.substring(0, 20));
    if (text.length() > 20)
      text = text.substring(20);
    else
      text = "";
  }
}

// Web menolak data pemeriksaan; mengirim ulang tidak akan membantu.
void showMeasurementRejected(const String &reason)
{
  lcdClear();
  lcdPrintLine(0, "DATA DITOLAK WEB");
  lcdPrintWrapped(1, reason);
  beepError();
  Serial.println("Data ditolak web: " + reason);
  delay(4000);
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

  // FIX: non-blocking - library MFRC522 memakai SPI 4 MHz secara default.
  // Catat di Serial agar konfigurasi kecepatan mudah diverifikasi saat boot.
  Serial.println("RFID SPI: 4 MHz");

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
// RESET STATE DAN KEMBALI KE STANDBY
// =============================================================

void resetMeasurementVariables()
{
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
}

// Satu-satunya jalan kembali ke STANDBY. Kartu yang sedang diproses dibatalkan
// dan semua antrean dikosongkan, sehingga hasil/kartu lama tidak bisa memicu
// pemeriksaan baru tanpa kartu ditempel lagi.
void returnToStandby()
{
  activeCardSeq = 0; // batalkan pengecekan web yang masih berjalan
  xQueueReset(lookupJobQueue);
  xQueueReset(cardEventQueue);
  xQueueReset(rfidResultQueue);

  resetMeasurementVariables();

  lastModePollAt = millis();
  currentState = STATE_STANDBY;
  showStandby();
}

// =============================================================
// TASK RFID (Core 1, prioritas tinggi): hanya membaca kartu
// =============================================================

// Kartu terbaca -> kirim ke loop() (mulai menimbang) dan ke task jaringan
// (cek ke web) SEKETIKA, lalu bunyi. Tidak ada HTTP maupun delay panjang di sini.
void taskRFID(void *parameter)
{
  (void)parameter;
  Serial.printf("taskRFID aktif di Core %d\n", xPortGetCoreID());

  // Setelah kartu diterima, kartu itu harus dilepas dulu dari pembaca sebelum
  // bisa dibaca lagi. Mencegah satu kartu yang menempel memicu pemeriksaan
  // berulang (kartu "magic"/clone bisa bangun lagi dari mode HALT).
  bool cardLatched = false;
  unsigned long lastCardSeenAt = 0;

  for (;;) {
    if (!systemReady || portalActive || pendingMeasurement || currentState != STATE_STANDBY) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    recoverRFIDIfNeeded();

    if (!rfidReady) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (cardLatched) {
      // WUPA membangunkan kartu yang sedang HALT; jawaban berarti kartu masih ada.
      byte atqa[2];
      byte atqaSize = sizeof(atqa);
      auto status = rfid.PICC_WakeupA(atqa, &atqaSize);
      if (status == MFRC522::STATUS_OK || status == MFRC522::STATUS_COLLISION) {
        rfid.PICC_HaltA();
        lastCardSeenAt = millis();
      } else if (millis() - lastCardSeenAt >= CARD_REMOVE_CONFIRM_MS) {
        cardLatched = false;
      }
      vTaskDelay(pdMS_TO_TICKS(60));
      continue;
    }

    if (!rfid.PICC_IsNewCardPresent()) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    unsigned long detectedAt = millis();
    if (!rfid.PICC_ReadCardSerial()) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    String uid = readRFIDUID();

    // Selesaikan transaksi SPI segera.
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

    cardLatched = true;
    lastCardSeenAt = millis();

    LookupJob job = {};
    strlcpy(job.uid, uid.c_str(), sizeof(job.uid));
    job.seq = ++cardSeqCounter;
    if (job.seq == 0) job.seq = ++cardSeqCounter; // 0 dicadangkan untuk "tidak ada kartu"
    activeCardSeq = job.seq;

    xQueueOverwrite(cardEventQueue, &job); // loop(): mulai pemeriksaan sekarang juga
    xQueueOverwrite(lookupJobQueue, &job); // task jaringan: cek kartu ke web

    // Respons lokal PERTAMA: bunyi, tanpa menunggu jaringan.
    beepCardDetected();

    Serial.printf("Card detected in %lu ms | UID: %s | seq: %lu\n",
                  millis() - detectedAt, uid.c_str(), (unsigned long)job.seq);
  }
}

// =============================================================
// TASK JARINGAN (Core 0, prioritas rendah): web boleh lambat
// =============================================================

// Tahan WiFi agar tidak dipaksa reconnect terus-menerus.
void kickWiFiReconnect()
{
  if (millis() - lastReconnectKickAt < 5000) return;
  lastReconnectKickAt = millis();
  WiFi.reconnect();
}

// Tidur yang langsung berhenti bila kartu ini sudah dibatalkan.
bool sleepWhileJobActive(uint32_t seq, uint32_t ms)
{
  for (uint32_t waited = 0; waited < ms; waited += 100) {
    if (seq != activeCardSeq) return false;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return seq == activeCardSeq;
}

void processLookupJob(const LookupJob &job)
{
  while (job.seq == activeCardSeq) {
    if (portalActive) {
      if (!sleepWhileJobActive(job.seq, 500)) return;
      continue;
    }

    // WiFi mati tidak boleh menahan apa pun; coba lagi sebentar lagi.
    if (WiFi.status() != WL_CONNECTED) {
      kickWiFiReconnect();
      if (!sleepWhileJobActive(job.seq, 2000)) return;
      continue;
    }

    String resultName;
    String resultSex;
    int resultAgeMonth = 0;
    RFIDCheckResult result = checkRFID(
      String(job.uid),
      resultName,
      resultSex,
      resultAgeMonth
    );

    // Kartu dibatalkan (pemeriksaan sudah selesai/ganti) selama menunggu web.
    if (job.seq != activeCardSeq) return;

    if (result != RFID_API_ERROR) {
      RFIDResultData completed = {};
      completed.result = result;
      completed.seq = job.seq;
      completed.childAgeMonth = resultAgeMonth;
      strlcpy(completed.uid, job.uid, sizeof(completed.uid));
      strlcpy(completed.childName, resultName.c_str(), sizeof(completed.childName));
      strlcpy(completed.childSex, resultSex.c_str(), sizeof(completed.childSex));
      xQueueSend(rfidResultQueue, &completed, 0);
      Serial.printf("Cek kartu selesai di background | UID: %s\n", job.uid);
      return;
    }

    Serial.println("Cek kartu ke web gagal, coba lagi 3 detik");
    if (!sleepWhileJobActive(job.seq, 3000)) return;
  }
}

// Heartbeat dan polling mode tangkap dikerjakan di sini, bukan di loop(),
// supaya request jaringan tidak pernah menghentikan penimbangan/pengukuran.
void networkHousekeeping()
{
  if (!systemReady || portalActive) return;

  if (WiFi.status() != WL_CONNECTED) {
    kickWiFiReconnect();
    return;
  }

  unsigned long now = millis();
  if (!pendingMeasurement && (lastHeartbeatAt == 0 || now - lastHeartbeatAt >= 10000)) {
    lastHeartbeatAt = now;
    sendHeartbeat();
    return;
  }

  if (currentState == STATE_STANDBY && activeCardSeq == 0)
    pollCaptureModeBackground();
}

void taskUpload(void *parameter)
{
  (void)parameter;
  Serial.printf("taskUpload aktif di Core %d\n", xPortGetCoreID());

  for (;;) {
    LookupJob job;
    if (xQueueReceive(lookupJobQueue, &job, pdMS_TO_TICKS(100)) == pdTRUE)
      processLookupJob(job);

    networkHousekeeping();
  }
}

// =============================================================
// SETUP
// =============================================================

void setup()
{
  Serial.begin(115200);

  // Queue dan mutex dibuat sebelum task/perangkat aktif.
  apiMutex = xSemaphoreCreateMutex();
  lookupJobQueue = xQueueCreate(1, sizeof(LookupJob));
  cardEventQueue = xQueueCreate(1, sizeof(LookupJob));
  rfidResultQueue = xQueueCreate(4, sizeof(RFIDResultData));
  if (!apiMutex || !lookupJobQueue || !cardEventQueue || !rfidResultQueue) {
    Serial.println("FATAL: alokasi queue/mutex RFID gagal");
    while (true) delay(1000);
  }

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

  // Arus pin maksimum (default ~20 mA, maksimum ~40 mA) agar buzzer lebih keras.
  gpio_set_drive_capability((gpio_num_t)BUZZER_PIN, GPIO_DRIVE_CAP_3);

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

  // Pembaca RFID hidup di Core 1 (menunggu systemReady sebelum membaca kartu).
  BaseType_t rfidTaskCreated = xTaskCreatePinnedToCore(
    taskRFID,
    "taskRFID",
    6144,
    nullptr,
    3,
    &taskRFIDHandle,
    1
  );
  if (rfidTaskCreated != pdPASS)
    Serial.println("FATAL: taskRFID gagal dibuat");

  // ---------------- WIFI ----------------

  connectWiFi();

  // Semua request web (cek kartu, heartbeat, mode) dikerjakan Core 0 dengan
  // prioritas lebih rendah daripada pembacaan RFID.
  BaseType_t uploadTaskCreated = xTaskCreatePinnedToCore(
    taskUpload,
    "taskUpload",
    12288,
    nullptr,
    1,
    &taskUploadHandle,
    0
  );
  if (uploadTaskCreated != pdPASS)
    Serial.println("FATAL: taskUpload gagal dibuat");

  // ---------------- STANDBY ----------------

  showStandby();

  Serial.println();
  Serial.println(
    "SISTEM SIAP"
  );

  // Bunyi pertama: alat menyala dan semua sensor siap dipakai.
  if (rfidReady && scaleReady && tofReady && lcdReady)
    beepDeviceReady();
  else
    beepError();

  // Buka pembacaan kartu setelah bunyi siap selesai.
  systemReady = true;
}

// =============================================================
// ALUR PEMERIKSAAN
// =============================================================

// Kartu diterima: langsung minta anak naik timbangan. Pengecekan web
// berjalan paralel di latar belakang; nama dan umur muncul bila sudah tiba.
void startMeasurement()
{
  childName = "";
  childSex = "";
  childAgeMonth = 0;

  lcdClear();

  lcdPrintLine(0, "SILAKAN NAIK");
  lcdPrintLine(1, "Berat: --.-- kg");
  lcdPrintLine(2, "Tunggu stabil...");
  lcdPrintLine(3, "Jangan bergerak");

  previousWeight = 0;
  stableWeightStart = 0;
  weightStable = false;

  currentState = STATE_WEIGHING;
}

void applyChildInfo(const RFIDResultData &r)
{
  childName = String(r.childName);
  childSex = String(r.childSex);
  childAgeMonth = r.childAgeMonth;

  // Layar menimbang hanya memperbarui baris 1-2, jadi baris 0 dan 3 aman diisi.
  if (currentState == STATE_WEIGHING)
  {
    lcdPrintLine(0, childName);
    lcdPrintLine(3, "Umur: " + String(childAgeMonth) + " bulan");
  }
}

// Jawaban web untuk kartu yang sedang diproses. Dipanggil dari loop().
void handleLookupResult(const RFIDResultData &r)
{
  // Hasil basi (kartu lain/sudah selesai) dibuang.
  if (activeCardSeq == 0 || r.seq != activeCardSeq)
    return;

  SystemState state = currentState;
  bool stillMeasuring =
    state == STATE_AWAIT_LOOKUP ||
    state == STATE_WEIGHING ||
    state == STATE_WEIGHT_LOCK ||
    state == STATE_HEIGHT_MEASURING;

  // Data sudah terkirim ke web: tidak ada yang perlu dibatalkan lagi.
  if (!stillMeasuring)
    return;

  switch (r.result)
  {
    case RFID_REGISTERED:
      if (state == STATE_AWAIT_LOOKUP)
        startMeasurement(); // mode tangkap sudah berakhir di web: lanjut menimbang
      applyChildInfo(r);
      return;

    case RFID_CAPTURED_REGISTERED:
      showCaptureAlreadyRegistered();
      captureModeCode = CAPTURE_OFF;
      returnToStandby();
      return;

    case RFID_CAPTURED:
      // Website meminta kartu (daftar/pilih balita): UID sudah dikirim,
      // batalkan penimbangan bila sempat dimulai.
      showCaptureDone();
      captureModeCode = CAPTURE_OFF;
      returnToStandby();
      return;

    case RFID_UNKNOWN:
      showUnknownRFID();
      returnToStandby();
      return;

    case RFID_INELIGIBLE:
      showIneligibleRFID();
      returnToStandby();
      return;

    default:
      return;
  }
}

void handleStandby()
{
  // Kartu baru dari taskRFID: respons segera, jangan menunggu web.
  LookupJob card;
  if (xQueueReceive(cardEventQueue, &card, 0) == pdTRUE)
  {
    if (card.seq != activeCardSeq)
      return; // kartu basi

    currentUID = String(card.uid);

    if (captureModeCode != CAPTURE_OFF)
    {
      // Web meminta kartu: alat tidak menimbang, tunggu jawaban web.
      awaitLookupStartedAt = millis();
      currentState = STATE_AWAIT_LOOKUP;
      showAwaitLookup();
    }
    else
    {
      startMeasurement();
    }
    return;
  }

  if (captureModeChanged)
  {
    captureModeChanged = false;
    showStandby();
  }
}

void handleAwaitLookup()
{
  if (millis() - awaitLookupStartedAt >= LOOKUP_WAIT_TIMEOUT_MS)
  {
    showApiTrouble();
    returnToStandby();
  }
}

void handleWeighing()
{
  if (processWeight())
  {
    finalWeight = stableWeight;

    currentState = STATE_WEIGHT_LOCK;

    stateStartTime = millis();
  }
}

void handleWeightLock()
{
  showWeightLock();

  resetHeightBuffer();

  lcdClear();

  lcdPrintLine(0, "PENGUKURAN TINGGI");
  lcdPrintLine(1, "Tinggi: --.-- cm");
  lcdPrintLine(2, "Berdiri tegak");
  lcdPrintLine(3, "Menunggu stabil...");

  delay(500);

  currentState = STATE_HEIGHT_MEASURING;
}

void handleHeightMeasuring()
{
  if (!processHeight())
    return;

  finalHeight = stableHeight;

  Serial.println();
  Serial.println("TINGGI TERKUNCI");

  Serial.print("Tinggi presisi: ");
  Serial.print(finalHeight, 2);
  Serial.println(" cm");

  beepHeightLocked();

  lcdClear();

  lcdPrintLine(0, "TINGGI TERKUNCI");
  lcdPrintLine(1, "Tinggi: " + String(finalHeight, 2) + " cm");
  lcdPrintLine(2, "Menghitung TB/U");
  lcdPrintLine(3, "Mohon tunggu...");

  // Layar di atas tampil selama pengiriman; pengiriman boleh menunggu koneksi.
  SubmitResult result = sendMeasurement();

  if (result == SUBMIT_OK)
  {
    // Semua perhitungan selesai dan hasil dari web sudah diterima.
    beepResultReady();
    currentState = STATE_RESULT_Z;
    return;
  }

  if (result == SUBMIT_REJECTED)
  {
    showMeasurementRejected(submitRejectReason);
    returnToStandby();
    return;
  }

  // SUBMIT_RETRY: data sudah tersimpan di NVS dan akan dikirim ulang otomatis.
  lcdClear();

  lcdPrintLine(0, "GAGAL KIRIM DATA");
  lcdPrintLine(1, "Periksa WiFi/API");
  lcdPrintLine(2, "Data disimpan,");
  lcdPrintLine(3, "dikirim otomatis");

  beepError();

  delay(3000);

  returnToStandby();
}

void handleResultZ()
{
  showZScoreResult();

  currentState = STATE_RESULT_ALL;
}

void handleResultAll()
{
  showAllResultPage1();

  delay(500);

  showAllResultPage2();

  currentState = STATE_FINISH;

  showFinish();
}

void handleFinish()
{
  // Tunggu sampai anak turun dari timbangan.
  if (!isWeightEmpty())
    return;

  delay(1000);

  // Pastikan benar-benar kosong.
  if (isWeightEmpty())
  {
    Serial.println("Anak sudah turun");
    returnToStandby();
  }
}

// Pemeriksaan yang gagal terkirim (tidak ada internet) disimpan di NVS dan
// dicoba lagi tiap 5 detik. Alat tidak menerima pengukuran baru sampai terkirim.
void handlePendingMeasurement()
{
  if (lastPendingRetryAt != 0 && millis() - lastPendingRetryAt < 5000)
    return;

  lastPendingRetryAt = millis();

  lcdPrintLine(0, "DATA BELUM TERKIRIM");
  lcdPrintLine(1, "Menunggu internet...");
  lcdPrintLine(2, pendingUid);
  lcdPrintLine(3, "Jangan ukur dahulu");

  SubmitResult result = submitMeasurement(pendingUid, pendingWeight, pendingHeight, pendingRequestId);

  if (result == SUBMIT_OK)
  {
    clearPendingMeasurement();
    lcdClear();
    lcdPrintLine(0, "DATA TERSINKRON");
    lcdPrintLine(1, "Z: " + String(finalZScore, 2));
    lcdPrintLine(2, finalCategory);
    delay(3000);
    returnToStandby();
  }
  else if (result == SUBMIT_REJECTED)
  {
    // Ditolak web: dikirim ulang tidak akan berhasil, jangan tahan alat selamanya.
    clearPendingMeasurement();
    showMeasurementRejected(submitRejectReason);
    returnToStandby();
  }
}

// =============================================================
// LOOP
// =============================================================

void loop()
{
  // Status sensor untuk heartbeat (pengiriman heartbeat ada di task jaringan).
  if (lastSensorCheckAt == 0 || millis() - lastSensorCheckAt >= 10000)
  {
    lastSensorCheckAt = millis();
    scaleReady = scale.is_ready();
    lcdReady = isLCDReady();
  }

  // Token ditolak berulang kali: buka portal konfigurasi (memakai LCD).
  if (portalRequested && currentState == STATE_STANDBY && !pendingMeasurement)
  {
    portalRequested = false;
    portalActive = true;

    if (apiMutex && xSemaphoreTake(apiMutex, pdMS_TO_TICKS(10000)) == pdTRUE)
    {
      secureClient.stop(); // bebaskan memori TLS sebelum portal konfigurasi
      xSemaphoreGive(apiMutex);
    }

    connectWiFi(true);
    returnToStandby();
    return;
  }

  if (pendingMeasurement)
  {
    handlePendingMeasurement();
    return;
  }

  if (currentState == STATE_STANDBY)
  {
    handleStandby();
    return;
  }

  // Jawaban web untuk kartu aktif; bisa membatalkan pengukuran (kartu tidak
  // dikenal, usia tidak sesuai, atau mode tangkap web).
  RFIDResultData lookup;
  while (xQueueReceive(rfidResultQueue, &lookup, 0) == pdTRUE)
    handleLookupResult(lookup);

  switch (currentState)
  {
    case STATE_STANDBY:          break; // dibatalkan oleh hasil web di atas
    case STATE_AWAIT_LOOKUP:     handleAwaitLookup();     break;
    case STATE_WEIGHING:         handleWeighing();        break;
    case STATE_WEIGHT_LOCK:      handleWeightLock();      break;
    case STATE_HEIGHT_MEASURING: handleHeightMeasuring(); break;
    case STATE_RESULT_Z:         handleResultZ();         break;
    case STATE_RESULT_ALL:       handleResultAll();       break;
    case STATE_FINISH:           handleFinish();          break;
  }
}
