# SIP Stunting IoT

Sistem informasi Posyandu berbasis PHP 8/MySQL yang terhubung dengan ESP32, RFID RC522, HX711, dan VL53L1X untuk pemeriksaan TB/U balita usia 24–60 bulan.

## Fitur

- Login admin dan petugas dengan pembatasan percobaan masuk.
- Data balita dan antrean UID RFID yang belum terdaftar.
- Pemeriksaan IoT dan manual dengan Z-score TB/U WHO harian.
- Riwayat immutable, pembatalan dengan alasan, grafik, laporan cetak/PDF, dan CSV.
- Registrasi perangkat bertoken, heartbeat, RSSI, serta status online/offline.
- API JSON HTTPS idempoten untuk ESP32.
- Portal konfigurasi Wi-Fi dan penyimpanan satu pemeriksaan tertunda di NVS.

## Struktur

- `public/`: document root web.
- `app/`: bootstrap, fungsi aplikasi, perhitungan WHO, dan API.
- `config/`: contoh konfigurasi; `local.php` tidak pernah masuk Git.
- `database/`: skema MySQL dan tabel LMS WHO.
- `firmware/`: sketch ESP32 dan daftar library.
- `tests/`: pengujian tanpa framework eksternal.

## Instalasi lokal

1. Buat database MySQL kosong.
2. Salin `config/config.example.php` menjadi `config/local.php`, lalu isi DSN, pengguna, password, URL, dan token setup acak minimal 32 karakter.
3. Arahkan document root server ke folder `public/`.
4. Buka `/setup`, masukkan token setup, simpan kredensial admin yang tampil satu kali, lalu login dan ganti password.
5. Jalankan pengujian dengan `php tests/run.php`.

Untuk server pengembangan bawaan PHP, sesuaikan `app.url` menjadi `http://127.0.0.1:8080`, set `secure_cookie` ke `false`, lalu jalankan:

```bash
php -S 127.0.0.1:8080 -t public public/index.php
```

## API perangkat

Semua request memakai header:

```text
Content-Type: application/json
X-Device-ID: ESP32-A1B2C3D4
Authorization: Bearer <token-yang-ditampilkan-menu-alat>
```

Endpoint: `POST /api/v1/heartbeat`, `POST /api/v1/rfid/lookup`, dan `POST /api/v1/measurements`.

## Firmware

Buka `firmware/kode_final.ino`, pasang library dalam `firmware/LIBRARIES.md`, pilih board ESP32, lalu upload. Pada boot pertama perangkat membuka hotspot konfigurasi. ID chip dan sandi hotspot tampil di LCD; buat perangkat dengan ID tersebut pada menu **Alat IoT**, lalu masukkan token yang dihasilkan ke portal.

## Keamanan

Jangan commit `config/local.php`, dump database produksi, token perangkat, atau kredensial Wi-Fi. Setelah setup produksi selesai, ganti `setup_token` pada konfigurasi lokal dengan nilai acak baru.

## Acuan

- WHO Child Growth Standards 2006 melalui paket resmi WHO `anthro`.
- Peraturan Menteri Kesehatan RI Nomor 2 Tahun 2020 tentang Standar Antropometri Anak.

