/*
===============================================================
 SISTEM IoT DETEKSI DINI STUNTING BALITA (Z-SCORE TB/U WHO)

 ESP32 + RFID RC522 + HX711 (berat) + VL53L1X (tinggi)
 + LCD 20x4 + Buzzer. Data dikirim ke web lewat API HTTPS.

 ALUR (satu kartu = satu pemeriksaan)
   1. Kartu ditempel  -> BUNYI dan layar "Silakan Naik" seketika
   2. Cek kartu ke web -> nama, jenis kelamin, umur tampil
   3. Berat stabil     -> terkunci
   4. Tinggi stabil    -> terkunci
   5. Kirim ke web     -> semua hasil tampil sekaligus + bunyi selesai

 Berat dan tinggi TIDAK dibulatkan. Angka di LCD saat terkunci
 sama persis dengan yang dikirim ke web (2 desimal, sama dengan
 kolom database).

 PIN
   VL53L1X  SDA 21, SCL 22
   LCD I2C  SDA 32, SCL 33 (PCF8574, alamat 0x27)
   HX711    DT 16, SCK 4
   RC522    SS 5, SCK 18, MOSI 19, MISO 23, RST 27
   Buzzer   GPIO 25
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
#include <driver/gpio.h>
#include <algorithm>
#include "HX711.h"
#include <VL53L1X.h>

// =============================================================
// KONFIGURASI
// =============================================================

const char* API_BASE_URL = "https://posyandu.oypoint.online/api/v1";
const char* FIRMWARE_VERSION = "2.1.0";
const char* DEVICE_ID = "SIP-STUNTING-01";

#define TOF_SDA 21
#define TOF_SCL 22
#define LCD_SDA 32
#define LCD_SCL 33
#define HX711_DT 16
#define HX711_SCK 4
#define RFID_SS 5
#define RFID_RST 27
#define RFID_SCK 18
#define RFID_MISO 23
#define RFID_MOSI 19
#define BUZZER_PIN 25

#define LCD_ADDRESS 0x27
#define LCD_RS 0x01
#define LCD_EN 0x04
#define LCD_BL 0x08

// Berat
const float CALIBRATION_FACTOR = 27312.27;
const float MIN_WEIGHT = 2.00;               // di bawah ini dianggap belum naik
const float STABLE_TOLERANCE = 0.10;         // selisih antar pembacaan (kg)
const unsigned long STABLE_TIME = 1500;      // harus stabil selama ini (ms)
const unsigned long WEIGHT_TIMEOUT = 120000; // batal bila tidak ada yang naik

// Tinggi (sensor dipasang 149.50 cm dari lantai)
const float SENSOR_HEIGHT_CM = 149.50;
const float MIN_HEIGHT_CM = 50.00;
const float MAX_HEIGHT_CM = 150.00;
const float HEIGHT_STABLE_TOLERANCE = 0.50;
const unsigned long HEIGHT_STABLE_TIME = 2000;
const unsigned long HEIGHT_TIMEOUT = 60000;
const int HEIGHT_SAMPLES = 7; // median 7 pembacaan terakhir

// Jaringan
const unsigned long HEARTBEAT_INTERVAL = 15000;
const unsigned long MODE_POLL_INTERVAL = 2000;

// ISRG Root X1 (Let's Encrypt), berlaku sampai 2035.
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

// =============================================================
// OBJEK DAN STATUS
// =============================================================

HX711 scale;
VL53L1X tof;
MFRC522 rfid(RFID_SS, RFID_RST);
TwoWire I2C_LCD = TwoWire(1);
Preferences prefs;
WiFiClientSecure secureClient;

String deviceToken;
bool rfidReady = false, scaleReady = false, tofReady = false, lcdReady = false;
bool secureClientReady = false;
int authFailures = 0;

// Mode tangkap kartu dari website (tombol "Tempel RFID" / "Via RFID").
enum CaptureMode { CAPTURE_OFF, CAPTURE_REGISTER, CAPTURE_SELECT };
CaptureMode captureMode = CAPTURE_OFF;
unsigned long modePollInterval = MODE_POLL_INTERVAL;

unsigned long lastHeartbeatAt = 0;
unsigned long lastModePollAt = 0;
unsigned long lastRfidCheckAt = 0;
unsigned long lastReconnectAt = 0;

// Satu kartu = satu pemeriksaan: kartu yang baru dipakai harus dilepas dulu.
bool cardMustLeave = false;
unsigned long cardSeenAt = 0;

// Data balita dari web
String childName, childSex;
int childAgeMonth = 0;

// Hasil dari web
float resultZ = 0;
String resultCategory;

// Pemeriksaan yang belum terkirim (tersimpan di NVS)
bool hasPending = false;
String pendingUid, pendingRequest;
float pendingWeight = 0, pendingHeight = 0;
unsigned long lastPendingTryAt = 0;

String rejectReason;

enum LookupResult { LOOKUP_REGISTERED, LOOKUP_UNKNOWN, LOOKUP_INELIGIBLE, LOOKUP_CAPTURED, LOOKUP_CAPTURED_REGISTERED, LOOKUP_ERROR };
enum SubmitResult { SUBMIT_OK, SUBMIT_RETRY, SUBMIT_REJECTED };

struct Note { uint16_t hz; uint16_t ms; }; // nada buzzer; hz 0 = jeda

// =============================================================
// LCD 20x4 (PCF8574, 4-bit)
// =============================================================

void lcdWrite(uint8_t data)
{
  I2C_LCD.beginTransmission(LCD_ADDRESS);
  I2C_LCD.write(data | LCD_BL);
  I2C_LCD.endTransmission();
}

void lcdNibble(uint8_t nibble, uint8_t mode)
{
  uint8_t data = (nibble << 4) | mode;
  lcdWrite(data | LCD_EN);
  delayMicroseconds(1);
  lcdWrite(data);
  delayMicroseconds(50);
}

void lcdByte(uint8_t value, uint8_t mode)
{
  lcdNibble(value >> 4, mode);
  lcdNibble(value & 0x0F, mode);
}

void lcdClear()
{
  lcdByte(0x01, 0);
  delay(2);
}

// Tulis satu baris penuh (sisa baris dibersihkan dengan spasi).
void lcdLine(uint8_t row, String text)
{
  static const uint8_t offset[] = {0x00, 0x40, 0x14, 0x54};
  lcdByte(0x80 | offset[row], 0);
  for (uint8_t i = 0; i < 20; i++)
    lcdByte(i < text.length() ? text[i] : ' ', LCD_RS);
}

void lcdShow(String l0, String l1 = "", String l2 = "", String l3 = "")
{
  lcdLine(0, l0);
  lcdLine(1, l1);
  lcdLine(2, l2);
  lcdLine(3, l3);
}

void lcdBegin()
{
  delay(50);
  lcdNibble(0x03, 0);
  delay(5);
  lcdNibble(0x03, 0);
  delayMicroseconds(150);
  lcdNibble(0x03, 0);
  lcdNibble(0x02, 0);
  lcdByte(0x28, 0); // 4-bit, 2 baris, 5x8
  lcdByte(0x0C, 0); // layar hidup
  lcdByte(0x06, 0);
  lcdClear();
}

bool isLCDReady()
{
  I2C_LCD.beginTransmission(LCD_ADDRESS);
  return I2C_LCD.endTransmission() == 0;
}

// =============================================================
// BUZZER
// =============================================================
// Buzzer ini paling nyaring di frekuensi rendah (sekitar 2 kHz); nada di atas
// 3 kHz terdengar mengecil. Semua nada penting memakai rentang 1.7-2.6 kHz.
// Ubah angka di tabel ini bila perlu menyetel suara.

const Note TUNE_CARD[]   = {{2000, 100}};
const Note TUNE_OK[]     = {{1800, 110}, {0, 80}, {2400, 180}};
const Note TUNE_WARN[]   = {{2300, 120}, {1700, 220}, {0, 100}, {2300, 120}, {1700, 220}};
const Note TUNE_ERROR[]  = {{1700, 170}, {0, 100}, {2300, 170}, {0, 100}, {1700, 170}, {0, 100}, {2300, 170}};
const Note TUNE_WEIGHT[] = {{1800, 130}, {0, 75}, {2600, 190}};
const Note TUNE_HEIGHT[] = {{1800, 90}, {0, 65}, {2200, 110}, {0, 65}, {2600, 230}};
const Note TUNE_READY[]  = {{1800, 250}, {0, 90}, {2200, 250}, {0, 90}, {2600, 600}};   // alat siap dipakai
const Note TUNE_DONE[]   = {{2000, 400}, {0, 120}, {2000, 400}, {0, 120}, {2000, 800}}; // semua hasil tampil

void play(const Note* tune, size_t count)
{
  for (size_t i = 0; i < count; i++)
  {
    ledcWriteTone(BUZZER_PIN, tune[i].hz);
    delay(tune[i].ms);
  }
  ledcWriteTone(BUZZER_PIN, 0);
}

#define PLAY(tune) play(tune, sizeof(tune) / sizeof(tune[0]))

// =============================================================
// WIFI DAN API
// =============================================================

// Menghubungkan WiFi lewat WiFiManager. Token perangkat dari website diisi
// pada portal ini (hotspot SIP-STUNTING-IOT, sandi SIPSTUNTING).
void connectWiFi(bool forcePortal = false)
{
  char tokenBuffer[97];
  deviceToken.substring(0, 96).toCharArray(tokenBuffer, sizeof(tokenBuffer));

  bool needsPortal = forcePortal || deviceToken.length() < 32;

  WiFiManager manager;
  WiFiManagerParameter tokenParam("token", "Token perangkat dari website", tokenBuffer, 96);
  manager.addParameter(&tokenParam);
  manager.setConfigPortalTimeout(180);
  manager.setConnectTimeout(20);

  bool showHotspot = needsPortal || WiFi.SSID().isEmpty();
  lcdShow(needsPortal ? "KONFIGURASI ALAT" : "MENGHUBUNGKAN WIFI", DEVICE_ID,
          showHotspot ? "SIP-STUNTING-IOT" : "Mohon tunggu...",
          showHotspot ? "Sandi: SIPSTUNTING" : "");

  bool connected = needsPortal
    ? manager.startConfigPortal("SIP-STUNTING-IOT", "SIPSTUNTING")
    : manager.autoConnect("SIP-STUNTING-IOT", "SIPSTUNTING");

  String submitted = String(tokenParam.getValue());
  submitted.trim();
  if (submitted.length() >= 32 && submitted != deviceToken)
  {
    deviceToken = submitted;
    prefs.putString("device_token", deviceToken);
  }

  if (connected)
  {
    WiFi.setSleep(false); // mode tidur modem menambah latensi tiap paket
    // Waktu harus benar agar sertifikat HTTPS valid.
    configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com");
    unsigned long started = millis();
    while (time(nullptr) < 1700000000 && millis() - started < 10000)
      delay(250);
  }

  lcdLine(3, connected ? "WiFi Terhubung" : "WiFi Gagal");
  Serial.println(connected ? "WiFi TERHUBUNG" : "WiFi GAGAL");
  delay(1200);
}

// Coba sambung ulang WiFi paling cepat tiap 5 detik, tanpa menunggu.
void kickWiFiReconnect()
{
  if (millis() - lastReconnectAt < 5000) return;
  lastReconnectAt = millis();
  WiFi.reconnect();
}

// Kirim JSON ke API. Mengembalikan kode HTTP (<= 0 bila gagal terhubung).
// waitWiFi = true: tunggu sampai 8 detik bila WiFi sedang putus.
int apiPost(const char* endpoint, JsonDocument& request, JsonDocument& response, bool waitWiFi = false)
{
  if (deviceToken.length() < 32) return -1;

  if (WiFi.status() != WL_CONNECTED)
  {
    if (!waitWiFi) return -1;
    WiFi.reconnect();
    unsigned long started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 8000) delay(250);
    if (WiFi.status() != WL_CONNECTED) return -1;
  }

  // Satu koneksi HTTPS dipakai ulang (keep-alive): handshake TLS memakan 1-3 detik.
  if (!secureClientReady)
  {
    secureClient.setCACert(ROOT_CA);
    secureClientReady = true;
  }

  String url = String(API_BASE_URL) + "/" + endpoint;
  String body;
  serializeJson(request, body);

  int code = -1;
  String raw;

  // Percobaan kedua hanya bila koneksi lama ternyata sudah ditutup server.
  for (int attempt = 0; attempt < 2; attempt++)
  {
    bool reused = secureClient.connected();

    HTTPClient http;
    http.setReuse(true);
    http.setConnectTimeout(2500);
    http.setTimeout(2500);
    if (!http.begin(secureClient, url)) return -1;

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("Authorization", "Bearer " + deviceToken);

    code = http.POST(body);
    raw = code > 0 ? http.getString() : "";
    http.end();

    if (code > 0) break;

    secureClient.stop(); // koneksi rusak: buang dan buka yang baru
    if (!reused) break;
  }

  if (code == 401 || code == 403) authFailures++;
  else if (code > 0) authFailures = 0;

  if (raw.length() > 0 && deserializeJson(response, raw))
  {
    Serial.println("JSON API tidak valid: " + raw);
    return -2;
  }
  return code;
}

// Cek kartu ke web: terdaftar, tidak dikenal, usia tidak sesuai, atau
// website sedang meminta kartu (mode tangkap).
LookupResult lookupCard(const String& uid)
{
  childName = "";
  childSex = "";
  childAgeMonth = 0;

  JsonDocument request;
  JsonDocument response;
  request["uid"] = uid;
  int code = apiPost("rfid/lookup", request, response);
  if (code <= 0) return LOOKUP_ERROR;

  String status = response["status"] | "error";

  if (status == "captured")
  {
    bool registered = response["registered"] | false;
    String mode = response["mode"] | "";
    return (mode == "register" && registered) ? LOOKUP_CAPTURED_REGISTERED : LOOKUP_CAPTURED;
  }
  if (status == "registered")
  {
    childName = response["child"]["name"].as<String>();
    childSex = response["child"]["sex"].as<String>();
    // Umur (bulan kalender) dihitung server; hari/30.4375 hanya cadangan.
    JsonObject child = response["child"];
    if (child["age_months"].is<int>())
      childAgeMonth = child["age_months"].as<int>();
    else
      childAgeMonth = (int)(child["age_days"].as<int>() / 30.4375f);
    return LOOKUP_REGISTERED;
  }
  if (status == "unknown") return LOOKUP_UNKNOWN;
  if (status == "ineligible" || status == "inactive") return LOOKUP_INELIGIBLE;
  return LOOKUP_ERROR;
}

// Kirim hasil pemeriksaan. Nilai dikirim apa adanya (2 desimal), tanpa pembulatan.
SubmitResult submitMeasurement(const String& uid, float weight, float height, const String& requestId)
{
  JsonDocument request;
  JsonDocument response;
  request["request_id"] = requestId;
  request["uid"] = uid;
  request["weight_kg"] = serialized(String(weight, 2));
  request["height_cm"] = serialized(String(height, 2));
  int code = apiPost("measurements", request, response, true);

  if (code == 200 && String(response["status"] | "") == "success")
  {
    resultZ = response["z_score"].as<float>();
    resultCategory = response["category"].as<String>();
    return SUBMIT_OK;
  }

  // Web menolak data (kartu tidak terdaftar, usia tidak sesuai, di luar rentang):
  // dikirim ulang tidak akan berhasil.
  if (code == 400 || code == 404 || code == 422)
  {
    rejectReason = response["error"]["message"] | "Data ditolak web";
    return SUBMIT_REJECTED;
  }

  return SUBMIT_RETRY; // tidak ada internet / server bermasalah
}

void sendHeartbeat()
{
  scaleReady = scale.is_ready();
  lcdReady = isLCDReady();

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
}

// =============================================================
// LAYAR STANDBY DAN MODE TANGKAP
// =============================================================

void showStandby()
{
  if (captureMode == CAPTURE_REGISTER)
    lcdShow("MODE PENDAFTARAN", "Tempelkan kartu", "balita yang akan", "didaftarkan di web");
  else if (captureMode == CAPTURE_SELECT)
    lcdShow("PILIH BALITA", "Tempelkan kartu", "untuk pemeriksaan", "(permintaan web)");
  else
    lcdShow("SISTEM IoT", "DETEKSI STUNTING", "Tempelkan RFID");
}

// Tanya web apakah sedang meminta kartu (hanya untuk pesan di LCD; kartu yang
// ditempel tetap dicek ke web apa pun hasilnya).
void pollCaptureMode()
{
  lastModePollAt = millis();

  JsonDocument request;
  JsonDocument response;
  request["ping"] = 1; // body harus objek JSON
  if (apiPost("mode", request, response) != 200)
  {
    modePollInterval = 10000; // koneksi bermasalah: jarangkan
    return;
  }
  modePollInterval = MODE_POLL_INTERVAL;

  String mode = response["mode"] | "off";
  CaptureMode newMode = CAPTURE_OFF;
  if (mode == "register") newMode = CAPTURE_REGISTER;
  else if (mode == "select") newMode = CAPTURE_SELECT;

  if (newMode != captureMode)
  {
    captureMode = newMode;
    showStandby();
  }
}

// Pekerjaan jaringan saat standby. Dipanggil bila tidak ada kartu; satu
// permintaan memakan sekitar 0.1-0.3 detik.
void idleNetwork()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    kickWiFiReconnect();
    return;
  }

  if (lastHeartbeatAt == 0 || millis() - lastHeartbeatAt >= HEARTBEAT_INTERVAL)
  {
    lastHeartbeatAt = millis();
    sendHeartbeat();

    // Token ditolak berulang kali: buka portal untuk mengisi token baru.
    if (authFailures >= 3)
    {
      authFailures = 0;
      secureClient.stop();
      connectWiFi(true);
      showStandby();
    }
    return;
  }

  if (millis() - lastModePollAt >= modePollInterval)
    pollCaptureMode();
}

// =============================================================
// RFID
// =============================================================

bool isRfidVersionValid(byte v)
{
  // 0x12/0x82/0x88 = clone RC522 yang umum, 0x90-0x92 = MFRC522 asli
  return v == 0x12 || v == 0x82 || v == 0x88 || v == 0x90 || v == 0x91 || v == 0x92;
}

bool initRFID()
{
  // Pastikan SS tidak aktif dan modul di-reset sebelum SPI dimulai.
  pinMode(RFID_SS, OUTPUT);
  digitalWrite(RFID_SS, HIGH);
  pinMode(RFID_RST, OUTPUT);
  digitalWrite(RFID_RST, LOW);
  delay(10);
  digitalWrite(RFID_RST, HIGH);
  delay(100);

  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  rfid.PCD_Init();
  delay(50);

  byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.printf("RFID RC522 VersionReg: 0x%02X\n", version);
  if (!isRfidVersionValid(version))
  {
    Serial.println("RFID RC522 tidak terdeteksi - periksa 3.3V dan kabel SPI");
    return false;
  }

  rfid.PCD_SetAntennaGain(MFRC522::RxGain_max); // lebih mudah membaca lewat casing
  rfid.PCD_AntennaOn();
  return true;
}

// Inisialisasi ulang bila komunikasi dengan modul RC522 terputus.
void checkRFIDHealth()
{
  if (millis() - lastRfidCheckAt < 5000) return;
  lastRfidCheckAt = millis();

  if (isRfidVersionValid(rfid.PCD_ReadRegister(MFRC522::VersionReg)))
    rfidReady = true;
  else
  {
    Serial.println("Komunikasi RFID terputus, inisialisasi ulang");
    rfidReady = initRFID();
  }
}

// WUPA membangunkan kartu yang sedang HALT: ada jawaban berarti kartu masih menempel.
bool cardStillPresent()
{
  byte atqa[2];
  byte size = sizeof(atqa);
  auto status = rfid.PICC_WakeupA(atqa, &size);
  if (status != MFRC522::STATUS_OK && status != MFRC522::STATUS_COLLISION) return false;
  rfid.PICC_HaltA();
  return true;
}

// Mengembalikan UID kartu yang baru ditempel, atau "" bila tidak ada.
String readCard()
{
  if (!rfidReady) return "";

  // Kartu dari pemeriksaan sebelumnya harus dilepas dulu (mencegah hitung ganda).
  if (cardMustLeave)
  {
    if (cardStillPresent()) cardSeenAt = millis();
    else if (millis() - cardSeenAt > 800) cardMustLeave = false;
    delay(50);
    return "";
  }

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return "";

  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++)
  {
    if (i > 0) uid += ":";
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return uid;
}

// =============================================================
// BERAT (HX711)
// =============================================================

// Berat TIDAK dibulatkan. Noise di bawah 0.10 kg dianggap nol agar timbangan
// kosong terdeteksi. Mengembalikan -1 bila HX711 belum siap.
float readWeight()
{
  if (!scale.is_ready()) return -1;

  float weight = scale.get_units(5);
  if (weight < 0.10) weight = 0;
  return weight;
}

// Tunggu berat stabil. Mengembalikan berat terkunci, atau -1 bila waktu habis.
float waitStableWeight()
{
  float previous = 0;
  unsigned long stableSince = 0;
  unsigned long started = millis();

  while (millis() - started < WEIGHT_TIMEOUT)
  {
    float weight = readWeight();
    if (weight < 0)
    {
      delay(1);
      continue;
    }

    lcdLine(1, "Berat: " + String(weight, 2) + " kg");
    lcdLine(2, "Menunggu stabil...");

    if (weight < MIN_WEIGHT) // belum ada yang naik
    {
      stableSince = 0;
    }
    else if (fabs(weight - previous) <= STABLE_TOLERANCE)
    {
      if (stableSince == 0) stableSince = millis();
      if (millis() - stableSince >= STABLE_TIME) return weight;
    }
    else
    {
      stableSince = millis();
    }
    previous = weight;
  }
  return -1;
}

// =============================================================
// TINGGI (VL53L1X)
// =============================================================

// Tinggi = tinggi sensor dari lantai - jarak ke kepala. Mengembalikan -1 bila
// pembacaan tidak valid atau di luar rentang.
float readHeight()
{
  uint16_t mm = tof.read();
  if (tof.timeoutOccurred()) return -1;

  // Data mentah sensor, untuk memeriksa akurasi tinggi (lihat Serial Monitor).
  Serial.printf("TOF jarak=%u mm status=%d sinyal=%.1f ambient=%.1f\n", mm,
                (int)tof.ranging_data.range_status,
                tof.ranging_data.peak_signal_count_rate_MCPS,
                tof.ranging_data.ambient_count_rate_MCPS);

  float height = SENSOR_HEIGHT_CM - mm / 10.0f;
  if (height < MIN_HEIGHT_CM || height > MAX_HEIGHT_CM) return -1;
  return height;
}

float medianOf(float* values, int count)
{
  float sorted[HEIGHT_SAMPLES];
  for (int i = 0; i < count; i++) sorted[i] = values[i];
  std::sort(sorted, sorted + count);
  if (count % 2 == 1) return sorted[count / 2];
  return (sorted[count / 2 - 1] + sorted[count / 2]) / 2.0f;
}

// Tunggu tinggi stabil. Mengembalikan tinggi terkunci, atau -1 bila waktu habis.
float waitStableHeight()
{
  float samples[HEIGHT_SAMPLES];
  int count = 0, next = 0;
  float previous = 0;
  unsigned long stableSince = 0;
  unsigned long started = millis();

  while (millis() - started < HEIGHT_TIMEOUT)
  {
    float raw = readHeight();
    if (raw <= 0)
    {
      previous = 0;
      lcdLine(1, "Tinggi: --.-- cm");
      lcdLine(2, "Posisi tidak sesuai");
      continue;
    }

    samples[next] = raw;
    next = (next + 1) % HEIGHT_SAMPLES;
    if (count < HEIGHT_SAMPLES) count++;
    float height = count < 3 ? raw : medianOf(samples, count);

    lcdLine(1, "Tinggi: " + String(height, 2) + " cm");
    lcdLine(2, "Menunggu stabil...");

    if (previous == 0)
    {
      stableSince = millis();
    }
    else if (fabs(height - previous) <= HEIGHT_STABLE_TOLERANCE)
    {
      if (millis() - stableSince >= HEIGHT_STABLE_TIME) return height;
    }
    else
    {
      stableSince = millis();
    }
    previous = height;
  }
  return -1;
}

// =============================================================
// PEMERIKSAAN TERTUNDA (NVS)
// =============================================================

void savePending(const String& uid, float weight, float height, const String& requestId)
{
  hasPending = true;
  pendingUid = uid;
  pendingWeight = weight;
  pendingHeight = height;
  pendingRequest = requestId;
  prefs.putBool("pending", true);
  prefs.putString("pending_uid", uid);
  prefs.putString("pending_req", requestId);
  prefs.putFloat("pending_w", weight);
  prefs.putFloat("pending_h", height);
}

void clearPending()
{
  hasPending = false;
  prefs.remove("pending");
  prefs.remove("pending_uid");
  prefs.remove("pending_req");
  prefs.remove("pending_w");
  prefs.remove("pending_h");
}

// Alat tidak menerima pengukuran baru sebelum data tertunda terkirim.
void retryPending()
{
  if (lastPendingTryAt != 0 && millis() - lastPendingTryAt < 5000) return;
  lastPendingTryAt = millis();

  lcdShow("DATA BELUM TERKIRIM", "Menunggu internet...", pendingUid, "Jangan ukur dahulu");

  SubmitResult result = submitMeasurement(pendingUid, pendingWeight, pendingHeight, pendingRequest);

  if (result == SUBMIT_OK)
  {
    clearPending();
    lcdShow("DATA TERSINKRON", "Z: " + String(resultZ, 2), resultCategory);
    delay(3000);
    showStandby();
  }
  else if (result == SUBMIT_REJECTED)
  {
    clearPending(); // ditolak web: jangan menahan alat selamanya
    lcdShow("DATA DITOLAK WEB", rejectReason.substring(0, 20), rejectReason.substring(20, 40), rejectReason.substring(40, 60));
    PLAY(TUNE_ERROR);
    delay(4000);
    showStandby();
  }
}

// =============================================================
// PEMERIKSAAN
// =============================================================

// Semua hasil tampil sekaligus: berat, tinggi, Z-Score, dan status.
void showResults(float weight, float height)
{
  lcdShow("Berat : " + String(weight, 2) + " kg",
          "Tinggi: " + String(height, 2) + " cm",
          "Z-Score: " + String(resultZ, 2) + " SD",
          resultCategory);
  PLAY(TUNE_DONE);
}

String sexText()
{
  if (childSex == "L") return "Laki-laki";
  if (childSex == "P") return "Perempuan";
  return childSex;
}

// Satu pemeriksaan lengkap untuk satu kartu. Kembali ke standby setelah selesai.
void examine(const String& uid)
{
  // 1. Respons lokal PERTAMA: bunyi dan layar, sebelum menghubungi web.
  PLAY(TUNE_CARD);
  if (captureMode != CAPTURE_OFF)
    lcdShow("KARTU TERBACA", uid, "Mengirim ke web...");
  else
    lcdShow("SILAKAN NAIK", "Berat: --.-- kg", "Cek kartu ke web...", "Jangan bergerak");

  // 2. Cek kartu ke web.
  LookupResult found = lookupCard(uid);

  if (found == LOOKUP_CAPTURED)
  {
    // Website meminta kartu (daftar/pilih balita): UID sudah terkirim, tidak menimbang.
    captureMode = CAPTURE_OFF;
    lcdShow("KARTU TERBACA", uid, "Dikirim ke website", "Lanjutkan di web");
    PLAY(TUNE_OK);
    delay(1200);
    return;
  }
  if (found == LOOKUP_CAPTURED_REGISTERED)
  {
    captureMode = CAPTURE_OFF;
    lcdShow("KARTU SUDAH", "TERDAFTAR", uid, "Pilih kartu lain");
    PLAY(TUNE_WARN);
    delay(3000);
    return;
  }
  if (found == LOOKUP_UNKNOWN)
  {
    lcdShow("RFID BELUM TERDAFTAR", uid, "Buka Website, menu", "Data Balita");
    PLAY(TUNE_ERROR);
    delay(2500);
    return;
  }
  if (found == LOOKUP_INELIGIBLE)
  {
    lcdShow("TIDAK DAPAT DIUKUR", "Usia harus 24-60", "bulan / data nonaktif");
    PLAY(TUNE_ERROR);
    delay(3000);
    return;
  }
  if (found == LOOKUP_ERROR && captureMode != CAPTURE_OFF)
  {
    lcdShow("GANGGUAN API", "Kartu tidak terkirim", "Coba kembali");
    PLAY(TUNE_ERROR);
    delay(3000);
    return;
  }
  // LOOKUP_REGISTERED, atau web tidak terjangkau: tetap ukur; web memeriksa
  // kartu saat hasil dikirim (jika gagal kirim, data disimpan di NVS).

  // 3. Data balita tampil lebih dulu: nama, jenis kelamin, umur. Baru menimbang.
  if (found == LOOKUP_REGISTERED)
  {
    lcdShow("DATA DITEMUKAN", childName, sexText() + " | " + String(childAgeMonth) + " bulan", "Silakan naik timbangan");
    PLAY(TUNE_OK);
    delay(2000);
    lcdShow(childName, "Berat: --.-- kg", "Menunggu stabil...", sexText() + " | " + String(childAgeMonth) + " bulan");
  }
  else
  {
    lcdShow("SILAKAN NAIK", "Berat: --.-- kg", "Menunggu stabil...", "Jangan bergerak");
  }

  float weight = waitStableWeight();
  if (weight < 0)
  {
    lcdShow("WAKTU HABIS", "Tidak ada yang naik", "timbangan");
    PLAY(TUNE_ERROR);
    delay(2500);
    return;
  }
  PLAY(TUNE_WEIGHT);

  // 4. Tinggi.
  lcdShow("PENGUKURAN TINGGI", "Tinggi: --.-- cm", "Berdiri tegak", "Menunggu stabil...");
  delay(500);

  float height = waitStableHeight();
  if (height < 0)
  {
    lcdShow("WAKTU HABIS", "Tinggi tidak stabil", "Ulangi pemeriksaan");
    PLAY(TUNE_ERROR);
    delay(2500);
    return;
  }
  Serial.printf("TINGGI TERKUNCI: %.2f cm | BERAT: %.2f kg\n", height, weight);
  PLAY(TUNE_HEIGHT);

  // 5. Kirim ke web.
  lcdShow("TINGGI TERKUNCI", "Tinggi: " + String(height, 2) + " cm", "Menghitung TB/U", "Mohon tunggu...");

  String requestId = String(DEVICE_ID) + "-" + String(millis(), HEX) + "-" + String(esp_random(), HEX);
  SubmitResult sent = submitMeasurement(uid, weight, height, requestId);

  if (sent == SUBMIT_OK)
  {
    showResults(weight, height); // semua hasil tampil + bunyi selesai

    // Tahan hasil minimal 8 detik, lalu tunggu anak turun dari timbangan.
    unsigned long shownAt = millis();
    while (millis() - shownAt < 8000) delay(100);
    while (millis() - shownAt < 60000)
    {
      float now = readWeight();
      if (now >= 0 && now < MIN_WEIGHT) break;
    }
  }
  else if (sent == SUBMIT_REJECTED)
  {
    lcdShow("DATA DITOLAK WEB", rejectReason.substring(0, 20), rejectReason.substring(20, 40), rejectReason.substring(40, 60));
    PLAY(TUNE_ERROR);
    Serial.println("Data ditolak web: " + rejectReason);
    delay(4000);
  }
  else
  {
    savePending(uid, weight, height, requestId);
    lcdShow("GAGAL KIRIM DATA", "Periksa WiFi/API", "Data disimpan,", "dikirim otomatis");
    PLAY(TUNE_ERROR);
    delay(3000);
  }
}

// =============================================================
// SETUP DAN LOOP
// =============================================================

void setup()
{
  Serial.begin(115200);

  prefs.begin("sip-stunting", false);
  deviceToken = prefs.getString("device_token", "");
  hasPending = prefs.getBool("pending", false);
  if (hasPending)
  {
    pendingUid = prefs.getString("pending_uid", "");
    pendingRequest = prefs.getString("pending_req", "");
    pendingWeight = prefs.getFloat("pending_w", 0);
    pendingHeight = prefs.getFloat("pending_h", 0);
  }

  delay(1000);
  Serial.println("\n=== SISTEM IoT DETEKSI DINI STUNTING ===");

  // LCD
  I2C_LCD.begin(LCD_SDA, LCD_SCL, 400000);
  lcdBegin();
  lcdReady = isLCDReady();
  lcdShow("SISTEM IoT", "DETEKSI STUNTING", "Inisialisasi...", "Mohon tunggu");

  // Buzzer; arus pin maksimum agar lebih keras
  ledcAttach(BUZZER_PIN, 2000, 8);
  gpio_set_drive_capability((gpio_num_t)BUZZER_PIN, GPIO_DRIVE_CAP_3);
  ledcWriteTone(BUZZER_PIN, 0);

  // Timbangan
  scale.begin(HX711_DT, HX711_SCK);
  scale.set_scale(CALIBRATION_FACTOR);
  lcdShow("INISIALISASI", "Tare timbangan...");
  delay(1000);
  scale.tare(20);
  scaleReady = scale.is_ready();
  Serial.println("HX711 siap");

  // Sensor tinggi (wajib ada)
  Wire.begin(TOF_SDA, TOF_SCL);
  tof.setTimeout(500);
  if (!tof.init())
  {
    lcdShow("ERROR VL53L1X", "Periksa kabel");
    while (true)
    {
      PLAY(TUNE_ERROR);
      delay(2000);
    }
  }
  tof.setDistanceMode(VL53L1X::Long);
  tof.setMeasurementTimingBudget(50000);
  tof.startContinuous(50);
  tofReady = true;
  Serial.println("VL53L1X siap");

  rfidReady = initRFID();

  connectWiFi();

  showStandby();
  Serial.println("SISTEM SIAP");

  // Bunyi pertama: alat menyala dan semua sensor siap dipakai.
  if (rfidReady && scaleReady && tofReady && lcdReady)
    PLAY(TUNE_READY);
  else
    PLAY(TUNE_ERROR);
}

void loop()
{
  if (hasPending)
  {
    retryPending();
    return;
  }

  String uid = readCard();
  if (uid != "")
  {
    examine(uid);

    cardMustLeave = true; // kartu ini tidak boleh memicu pemeriksaan lagi
    cardSeenAt = millis();
    showStandby();
    return;
  }

  checkRFIDHealth();
  idleNetwork();
}
