# Sistem Absensi RFID Berbasis ESP32

Proyek ini adalah sebuah sistem absensi sederhana yang dibangun menggunakan ESP32 dengan pembaca kartu RFID (RC522) untuk mencatat waktu masuk dan keluar. Data absensi disimpan secara online dan dapat diakses secara real-time.

![Demo Proyek](link-gambar-atau-gif-demo-proyekmu.jpg)
*(Tips: Kamu bisa upload gambar/gif demo proyekmu ke tab "Issues" di GitHub lalu salin link-nya ke sini agar terlihat keren)*

---

##  Daftar Fitur

* Absensi Masuk (Check-in) dan Keluar (Check-out) menggunakan kartu RFID.
* Data absensi dikirim dan disimpan ke database (Firebase).
* Dilengkapi dengan layar LCD 16x2 untuk menampilkan status dan waktu.
* Notifikasi real-time melalui (Buzzer).
  
---

## Kebutuhan Hardware

* ESP32 DevKit V1
* RFID Reader MFRC522 beserta Kartu/Tag RFID
* LCD I2C 16x2
* Buzzer (jika ada)
* Kabel Jumper
* Project Board / PCB

---

## Kebutuhan Software & Library

1.  **[Arduino IDE](https://www.arduino.cc/en/software)** atau **[PlatformIO](https://platformio.org/)**
2.  **Board ESP32**: Pastikan sudah terinstal di Arduino IDE melalui Board Manager.
3.  **Library yang dibutuhkan**:
    * `MFRC522.h` untuk RFID Reader
    * `LiquidCrystal_I2C.h` untuk layar LCD
    * `WiFi.h` (bawaan ESP32)
    * `FirebaseESP32.h` untuk konek ke Firebase

---

## Instalasi & Konfigurasi

1.  **Rangkai Sirkuit**: Hubungkan semua komponen hardware sesuai dengan skema rangkaian berikut.
    *(Tips: Buat skema rangkaian sederhana menggunakan Fritzing atau cukup tulis pin-out-nya, contoh:)*
    * **RC522** -> **ESP32**
        * SDA -> GPIO 21
        * SCK -> GPIO 18
        * ...dan seterusnya
    * **LCD I2C** -> **ESP32**
        * SDA -> GPIO 21
        * SCL -> GPIO 22

2.  **Konfigurasi Software**:
    * Clone repository ini: `git clone https://github.com/Raynard15/absensi-ESP32.git`
    * Buka file `Absensi.ino` menggunakan Arduino IDE.
    * Instal semua library yang dibutuhkan melalui Library Manager.
    * Ubah kredensial WiFi (SSID dan Password) di dalam kode.
    * Masukkan API Key dan konfigurasi database (Firebase/Google Sheets) di dalam kode.

3.  **Upload ke ESP32**:
    * Pilih board "ESP32 Dev Module" dan Port yang sesuai.
    * Klik tombol Upload.

---

## Cara Penggunaan

1.  Nyalakan perangkat ESP32.
2.  Tunggu hingga perangkat terhubung ke WiFi dan layar LCD menampilkan "Sistem Siap".
3.  Untuk absensi, cukup dekatkan kartu RFID terdaftar ke RFID reader.
4.  Layar akan menampilkan pesan sukses beserta nama dan jam absensi.
