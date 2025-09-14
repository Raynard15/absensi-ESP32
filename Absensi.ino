/*
 * ESP32 RFID Attendance & E-Money System with Firebase
 * Fitur:
 * - Absensi Check-in/Check-out dengan batasan waktu
 * - Fitur E-Money (Top Up, Bayar, Cek Saldo)
 * - Tampilan LCD 16x2
 * - Umpan balik Buzzer
 * - Pelacakan waktu dengan NTP
 * - Penyimpanan data ganda (EEPROM & Firebase)
 */

#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <time.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include <Firebase_ESP32_Client.h>

// =================================================================
// PENGATURAN FIREBASE - GANTI DENGAN DATA ANDA
// =================================================================
#define FIREBASE_HOST "https://presensi-9620f-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_API_KEY "GANTI_DENGAN_KUNCI_API_WEB_ANDA" 

// Objek Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseJson json; // Objek JSON global untuk efisiensi memori

// =================================================================
// DEFINISI PIN
// =================================================================
#define SS_PIN 5
#define RST_PIN 4   // DIPINDAHKAN ke pin 4 untuk menghindari konflik
#define BUZZER_PIN 2
#define SDA_PIN 26
#define SCL_PIN 25

// Objek Perangkat Keras
MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Ganti alamat I2C jika perlu (misal: 0x3F)

// =================================================================
// KREDENSIAL & PENGATURAN
// =================================================================
const char* ssid = "NAMA_WIFI_ANDA";
const char* password = "PASSWORD_WIFI_ANDA";

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;  // GMT+7
const int daylightOffset_sec = 0;

#define EEPROM_SIZE 4096
#define USER_COUNT_ADDR 0
#define USER_DATA_START 4
#define MAX_USERS 50

// Struktur data pengguna diperbarui dengan saldo
struct UserData {
  char uid[24];
  char name[20];
  bool isCheckedIn;
  time_t lastCheckIn;
  time_t lastCheckOut;
  long balance; // <- Saldo E-Money
};

UserData users[MAX_USERS];
int userCount = 0;
bool firebaseConnected = false;

// Deklarasi fungsi
void playSuccessSound();
void playErrorSound();
void playTripleBeepSound();
void listAllUsers();
void deleteUser();
bool initializeFirebase();
void topUpBalance();
void makePayment();
void checkBalance();


void setup() {
  Serial.begin(115200);
  
  EEPROM.begin(EEPROM_SIZE);
  
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");
  
  SPI.begin();
  rfid.PCD_Init();
  
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    firebaseConnected = initializeFirebase();
  } else {
    Serial.println("\nWiFi failed! Using offline mode");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed");
    delay(2000);
  }
  
  loadUserData();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("RFID Attendance");
  lcd.setCursor(0, 1);
  lcd.print("Tap your card");
  
  Serial.println("System ready!");
  Serial.println("Commands: add, list, delete, clear, topup, pay, balance");
  
  playSuccessSound();
}

bool initializeFirebase() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting DB...");
  
  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_HOST;
  
  Firebase.signUp(&config, &auth, "", "");
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  if (Firebase.ready()) {
    Serial.println("Firebase connection ready.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Firebase OK");
    delay(2000);
    return true;
  } else {
    Serial.println("Firebase connection failed.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Firebase FAIL");
    delay(2000);
    return false;
  }
}

void loop() {
  handleSerialCommands();
  
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String cardUID = getCardUID();
    int userIndex = findUserByUID(cardUID);
    
    if (userIndex >= 0) {
      handleAttendance(userIndex);
    } else {
      handleUnknownCard(cardUID);
    }
    
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    delay(3000); 

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RFID Attendance");
    lcd.setCursor(0, 1);
    lcd.print("Tap your card");
  }
}

String getCardUID() {
  String uidString = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uidString += (rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    uidString += String(rfid.uid.uidByte[i], HEX);
  }
  uidString.toUpperCase();
  return uidString;
}

int findUserByUID(String uid) {
  for (int i = 0; i < userCount; i++) {
    if (strcmp(uid.c_str(), users[i].uid) == 0) {
      return i;
    }
  }
  return -1;
}

void handleAttendance(int userIndex) {
  UserData &user = users[userIndex];
  
  time_t now = time(nullptr);
  struct tm* timeInfo = localtime(&now);

  char timeStr[9];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeInfo);
  
  char datePath[11];
  strftime(datePath, sizeof(datePath), "%Y-%m-%d", timeInfo);

  lcd.clear();
  
  if (!user.isCheckedIn) {
    // Proses Check-In
    user.isCheckedIn = true;
    user.lastCheckIn = now;
    
    lcd.setCursor(0, 0);
    lcd.print(user.name);
    lcd.setCursor(0, 1);
    lcd.print("IN: ");
    lcd.print(timeStr);
    
    Serial.println("CHECK-IN: " + String(user.name) + " at " + timeStr);
    tone(BUZZER_PIN, 800, 300);
    
    if (firebaseConnected && Firebase.ready()) {
      String path = "/attendance/" + String(datePath) + "/" + String(user.uid);
      json.clear();
      json.set("name", user.name);
      json.set("check_in", timeStr);
      json.set("check_out", "N/A");
      json.set("work_duration", "N/A");
      if (!Firebase.RTDB.setJSON(&fbdo, path, &json)) {
          Serial.println("Failed to send check-in data: " + fbdo.errorReason());
      } else {
          Serial.println("Check-in data sent successfully.");
      }
    }
    
  } else {
    // Proses Check-Out
    if (timeInfo->tm_hour < 16) {
      lcd.setCursor(0, 0);
      lcd.print("Belum bisa absen");
      lcd.setCursor(0, 1);
      lcd.print("pulang!");
      Serial.println("Gagal Check-out: Belum jam 16:00");
      playTripleBeepSound();
      return;
    }

    user.isCheckedIn = false;
    user.lastCheckOut = now;
    
    unsigned long workDuration = user.lastCheckOut - user.lastCheckIn;
    int hours = workDuration / 3600;
    int minutes = (workDuration % 3600) / 60;
    String durationStr = String(hours) + "h " + String(minutes) + "m";
    
    lcd.setCursor(0, 0);
    lcd.print(user.name);
    lcd.setCursor(0, 1);
    lcd.print("OUT: ");
    lcd.print(timeStr);
    delay(2000); 

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Durasi Kerja:");
    lcd.setCursor(0, 1);
    lcd.print(durationStr);

    Serial.println("CHECK-OUT: " + String(user.name) + " at " + timeStr);
    Serial.println("Durasi kerja: " + durationStr);
    tone(BUZZER_PIN, 1200, 200);

    if (firebaseConnected && Firebase.ready()) {
      String path = "/attendance/" + String(datePath) + "/" + String(user.uid);
      json.clear();
      json.set("name", user.name);
      json.set("check_out", timeStr);
      json.set("work_duration", durationStr);
      if (!Firebase.RTDB.updateNode(&fbdo, path, &json)) {
        Serial.println("Failed to send check-out data: " + fbdo.errorReason());
      } else {
        Serial.println("Check-out data sent successfully.");
      }
    }
  }
  
  saveUserData();
}

void handleUnknownCard(String uid) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Unknown Card");
  lcd.setCursor(0, 1);
  lcd.print(uid);
  
  Serial.println("Unknown card: " + uid);
  playErrorSound();
}

void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command == "add") addNewUser();
    else if (command == "list") listAllUsers();
    else if (command == "delete") deleteUser();
    else if (command == "clear") clearAllData();
    else if (command == "status") showSystemStatus();
    else if (command == "topup") topUpBalance();
    else if (command == "pay") makePayment();
    else if (command == "balance") checkBalance();
    else if(command.length() > 0) Serial.println("Unknown command.");
  }
}

void addNewUser() {
  if (userCount >= MAX_USERS) {
    Serial.println("User limit reached!");
    return;
  }
  
  Serial.println("=== ADD NEW USER ===");
  Serial.println("Please tap the RFID card...");
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ADD: Tap Card");

  unsigned long timeout = millis() + 10000;
  String cardUID = "";
  while (millis() < timeout) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      cardUID = getCardUID();
      break;
    }
  }

  if (cardUID == "") {
    Serial.println("Timeout!");
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Timeout!");
    delay(2000);
    return;
  }
  
  if (findUserByUID(cardUID) >= 0) {
    Serial.println("Card already registered!");
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Card exists!");
    delay(2000);
    return;
  }
  
  while(Serial.available()) Serial.read();

  Serial.print("Enter name: ");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter name in");
  lcd.setCursor(0, 1);
  lcd.print("Serial Monitor");

  String userName = "";
  timeout = millis() + 30000;
  while(millis() < timeout) {
      if(Serial.available()){
          userName = Serial.readStringUntil('\n');
          userName.trim();
          break;
      }
  }

  if (userName.length() == 0 || userName.length() > 19) {
    Serial.println("Invalid name!");
    return;
  }
  
  strcpy(users[userCount].uid, cardUID.c_str());
  strcpy(users[userCount].name, userName.c_str());
  users[userCount].isCheckedIn = false;
  users[userCount].lastCheckIn = 0;
  users[userCount].lastCheckOut = 0;
  users[userCount].balance = 0;
  userCount++;
  
  saveUserData();
  
  if (firebaseConnected && Firebase.ready()) {
    String path = "/users/" + cardUID;
    json.clear();
    json.set("name", userName);
    json.set("balance", 0);
    if (!Firebase.RTDB.setJSON(&fbdo, path, &json)) {
      Serial.println("Failed to send new user data: " + fbdo.errorReason());
    } else {
      Serial.println("New user data sent to Firebase successfully.");
    }
  }

  Serial.println("User added successfully!");
  Serial.println("UID: " + cardUID + ", Name: " + userName);
  
  lcd.clear();
  lcd.print("User Added!");
  lcd.setCursor(0, 1);
  lcd.print(userName);
  playSuccessSound();
  delay(2000);
}

void listAllUsers() {
  Serial.println("=== USER ATTENDANCE & BALANCE LOG ===");
  Serial.printf("Total users: %d/%d\n", userCount, MAX_USERS);
  Serial.println("--------------------------------------------------------------------------");
  Serial.println("Name            | Balance      | Status | Last Check-In       | Last Check-Out");
  Serial.println("--------------------------------------------------------------------------");
  
  char timeBuffer[20];

  for (int i = 0; i < userCount; i++) {
    Serial.printf("%-15s | Rp %-9ld | %-6s | ", users[i].name, users[i].balance, users[i].isCheckedIn ? "IN" : "OUT");

    if (users[i].lastCheckIn != 0) {
      strftime(timeBuffer, sizeof(timeBuffer), "%d/%m/%y %H:%M:%S", localtime(&users[i].lastCheckIn));
      Serial.printf("%-19s | ", timeBuffer);
    } else {
      Serial.print("N/A                 | ");
    }

    if (users[i].lastCheckOut != 0) {
      strftime(timeBuffer, sizeof(timeBuffer), "%d/%m/%y %H:%M:%S", localtime(&users[i].lastCheckOut));
      Serial.printf("%s\n", timeBuffer);
    } else {
      Serial.println("N/A");
    }
  }
  Serial.println("--------------------------------------------------------------------------");
}

void deleteUser() {
  Serial.println("=== DELETE USER ===");
  Serial.println("Please tap the RFID card to delete...");
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DELETE: Tap Card");

  unsigned long timeout = millis() + 10000;
  String cardUID = "";
  while (millis() < timeout) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      cardUID = getCardUID();
      break;
    }
  }

  if (cardUID == "") {
    Serial.println("Timeout! No card detected.");
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Timeout!");
    delay(2000);
    return;
  }

  int userIndex = findUserByUID(cardUID);

  if (userIndex == -1) {
    Serial.println("User not found!");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("User Not Found");
    delay(2000);
    return;
  }

  Serial.printf("User found: %s. Are you sure? Type 'YES' to confirm.\n", users[userIndex].name);
  lcd.clear();
  lcd.print("Delete ");
  lcd.print(users[userIndex].name);
  lcd.setCursor(0,1);
  lcd.print("Confirm in Serial");
  
  while(Serial.available()) Serial.read();

  timeout = millis() + 15000;
  while(millis() < timeout){
    if(Serial.available()){
      String confirm = Serial.readStringUntil('\n');
      confirm.trim();
      if (confirm == "YES") {
        if (firebaseConnected && Firebase.ready()) {
          String path = "/users/" + String(users[userIndex].uid);
          if (Firebase.RTDB.deleteNode(&fbdo, path)) {
            Serial.println("User deleted from Firebase.");
          } else {
            Serial.println("Failed to delete user from Firebase: " + fbdo.errorReason());
          }
        }

        for (int i = userIndex; i < userCount - 1; i++) {
          users[i] = users[i + 1];
        }
        userCount--;
        saveUserData();
        
        Serial.println("User deleted successfully!");
        lcd.clear();
        lcd.print("User Deleted!");
        tone(BUZZER_PIN, 600, 400);
        delay(2000);
      } else {
        Serial.println("Delete operation cancelled.");
        lcd.clear();
        lcd.print("Cancelled");
        delay(2000);
      }
      return;
    }
  }
  Serial.println("Timeout. Delete operation cancelled.");
  lcd.clear();
  lcd.print("Timeout!");
  delay(2000);
}


void clearAllData() {
  Serial.println("Type 'YES' to confirm clearing all data.");
  unsigned long timeout = millis() + 10000;
  while(millis() < timeout){
    if(Serial.available()){
      String confirm = Serial.readStringUntil('\n');
      confirm.trim();
      if (confirm == "YES") {
        userCount = 0;
        EEPROM.write(0, 0);
        EEPROM.commit();
        Serial.println("All data cleared from EEPROM!");

        if(firebaseConnected && Firebase.ready()){
            Firebase.RTDB.deleteNode(&fbdo, "/users");
            Firebase.RTDB.deleteNode(&fbdo, "/attendance");
            Serial.println("All data cleared from Firebase!");
        }

        lcd.clear();
        lcd.print("Data Cleared!");
        delay(2000);
      } else {
        Serial.println("Operation cancelled");
      }
      return;
    }
  }
  Serial.println("Timeout. Operation cancelled.");
}

void showSystemStatus() {
  Serial.println("=== SYSTEM STATUS ===");
  Serial.printf("Users: %d/%d\n", userCount, MAX_USERS);
  Serial.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  if (WiFi.status() == WL_CONNECTED) {
    time_t now = time(nullptr);
    Serial.printf("Current time: %s", ctime(&now));
  }
}

void loadUserData() {
  userCount = EEPROM.read(USER_COUNT_ADDR);
  if (userCount > MAX_USERS || userCount < 0) {
      userCount = 0;
  }
  for (int i = 0; i < userCount; i++) {
    int addr = USER_DATA_START + (i * sizeof(UserData));
    EEPROM.get(addr, users[i]);
  }
  Serial.printf("Loaded %d users from EEPROM\n", userCount);
}

void saveUserData() {
  EEPROM.write(USER_COUNT_ADDR, userCount);
  for (int i = 0; i < userCount; i++) {
    int addr = USER_DATA_START + (i * sizeof(UserData));
    EEPROM.put(addr, users[i]);
  }
  if (!EEPROM.commit()) {
    Serial.println("ERROR! EEPROM commit failed");
  } else {
    Serial.println("EEPROM data saved successfully.");
  }
}

void playSuccessSound() {
  tone(BUZZER_PIN, 1000, 150);
  delay(200);
  tone(BUZZER_PIN, 1500, 150);
}

void playErrorSound(){
  tone(BUZZER_PIN, 400, 500);
}

void playTripleBeepSound() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 300, 150);
    delay(200);
  }
}

void topUpBalance() {
  Serial.println("=== TOP UP BALANCE ===");
  Serial.println("Tap card to top up...");
  lcd.clear();
  lcd.print("TOP UP");
  lcd.setCursor(0, 1);
  lcd.print("Tap your card");

  unsigned long timeout = millis() + 10000;
  String cardUID = "";
  while (millis() < timeout) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      cardUID = getCardUID();
      break;
    }
  }

  if (cardUID == "") {
    Serial.println("Timeout!");
    lcd.clear();
    lcd.print("Timeout!");
    delay(2000);
    return;
  }

  int userIndex = findUserByUID(cardUID);
  if (userIndex == -1) {
    handleUnknownCard(cardUID);
    return;
  }

  while(Serial.available()) Serial.read();

  Serial.print("Enter top up amount: ");
  lcd.clear();
  lcd.print(users[userIndex].name);
  lcd.setCursor(0,1);
  lcd.print("Amount(Serial)?");
  
  String amountStr = "";
  timeout = millis() + 20000;
  while(millis() < timeout){
      if(Serial.available()){
          amountStr = Serial.readStringUntil('\n');
          amountStr.trim();
          break;
      }
  }

  if (amountStr.length() == 0) {
    Serial.println("Timeout waiting for amount!");
    return;
  }

  long amount = amountStr.toInt();
  if (amount <= 0) {
    Serial.println("Invalid amount!");
    return;
  }

  users[userIndex].balance += amount;
  saveUserData();

  if (firebaseConnected && Firebase.ready()) {
    String path = "/users/" + cardUID + "/balance";
    if(!Firebase.RTDB.setFloat(&fbdo, path, users[userIndex].balance)){
        Serial.println("Failed to update balance on Firebase: " + fbdo.errorReason());
    } else {
        Serial.println("Balance updated on Firebase.");
    }
  }

  Serial.printf("Top up successful for %s. New balance: %ld\n", users[userIndex].name, users[userIndex].balance);
  lcd.clear();
  lcd.print("Top Up Success!");
  lcd.setCursor(0, 1);
  lcd.printf("Rp %ld", users[userIndex].balance);
  playSuccessSound();
}

void makePayment() {
  Serial.println("=== MAKE PAYMENT ===");
  while(Serial.available()) Serial.read();
  
  Serial.print("Enter payment amount: ");
  lcd.clear();
  lcd.print("Enter Amount");
  lcd.setCursor(0,1);
  lcd.print("in Serial Monitor");
  
  String amountStr = "";
  unsigned long timeout = millis() + 20000;
  while(millis() < timeout){
      if(Serial.available()){
          amountStr = Serial.readStringUntil('\n');
          amountStr.trim();
          break;
      }
  }
  
  if (amountStr.length() == 0) {
    Serial.println("Timeout!");
    return;
  }

  long amount = amountStr.toInt();
  if (amount <= 0) {
    Serial.println("Invalid amount!");
    return;
  }

  Serial.println("Tap card to pay...");
  lcd.clear();
  lcd.print("PAYMENT");
  lcd.setCursor(0, 1);
  lcd.print("Tap your card");
  
  String cardUID = "";
  timeout = millis() + 10000;
  while (millis() < timeout) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      cardUID = getCardUID();
      break;
    }
  }

  if (cardUID == "") {
    Serial.println("Timeout!");
    lcd.clear();
    lcd.print("Timeout!");
    delay(2000);
    return;
  }

  int userIndex = findUserByUID(cardUID);
  if (userIndex == -1) {
    handleUnknownCard(cardUID);
    return;
  }

  if (users[userIndex].balance >= amount) {
    users[userIndex].balance -= amount;
    saveUserData();

    if (firebaseConnected && Firebase.ready()) {
      String path = "/users/" + cardUID + "/balance";
      Firebase.RTDB.setFloat(&fbdo, path, users[userIndex].balance);
    }
    
    Serial.printf("Payment of %ld successful for %s. New balance: %ld\n", amount, users[userIndex].name, users[userIndex].balance);
    lcd.clear();
    lcd.print("Payment Success!");
    lcd.setCursor(0, 1);
    lcd.printf("Rp %ld", users[userIndex].balance);
    playSuccessSound();
  } else {
    Serial.printf("Payment failed. Insufficient balance for %s.\n", users[userIndex].name);
    lcd.clear();
    lcd.print("Saldo Kurang!");
    lcd.setCursor(0, 1);
    lcd.printf("Rp %ld", users[userIndex].balance);
    playErrorSound();
  }
}

void checkBalance() {
  Serial.println("=== CHECK BALANCE ===");
  Serial.println("Tap card to check balance...");
  lcd.clear();
  lcd.print("CHECK BALANCE");
  lcd.setCursor(0, 1);
  lcd.print("Tap your card");

  unsigned long timeout = millis() + 10000;
  String cardUID = "";
  while (millis() < timeout) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      cardUID = getCardUID();
      break;
    }
  }

  if (cardUID == "") {
    Serial.println("Timeout!");
    lcd.clear();
    lcd.print("Timeout!");
    delay(2000);
    return;
  }

  int userIndex = findUserByUID(cardUID);
  if (userIndex == -1) {
    handleUnknownCard(cardUID);
    return;
  }

  Serial.printf("Balance for %s: %ld\n", users[userIndex].name, users[userIndex].balance);
  lcd.clear();
  lcd.print(users[userIndex].name);
  lcd.setCursor(0, 1);
  lcd.printf("Saldo: Rp %ld", users[userIndex].balance);
  tone(BUZZER_PIN, 1000, 200);
}

