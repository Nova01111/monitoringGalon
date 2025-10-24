#define BLYNK_TEMPLATE_ID "TMPL6DiXtwnct"
#define BLYNK_TEMPLATE_NAME "monitoringGalon"

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include "credential.h" // Pastikan file ini ada (berisi BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASS)

// ---------- Pin Lama ----------
#define TRIG_PIN D5
#define ECHO_PIN D6
#define BUZZER_PIN D4
#define LED_PIN D7

// ---------- Pin Baru ----------
#define RELAY_PIN D3     // Pin untuk mengontrol relay pompa
#define SAKLAR_PIN D0    // Pin untuk saklar/tombol manual (INPUT_PULLUP)

// ---------- LCD & Timer ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);
BlynkTimer timer;

// ---------- Parameter default ----------
float tinggiGelas = 6.5;    // cm
float diameter = 5.0;       // cm

const int SAMPLE_COUNT = 5;
const unsigned long PULSE_TIMEOUT = 30000;
const float ALPHA = 0.25;
const float MAX_DELTA = 0.5;
const float ALARM_ON = 3.0;  // Level air penuh (jarak sensor < 3.0 cm)
const float ALARM_OFF = 3.3; // Histeresis alarm

float smoothedDistance = -1;
float lastValidDistance = -1;
unsigned long lastUpdateLCD = 0;
const unsigned long lcdInterval = 200;
bool buzzerEnabled = true;

// ---------- Virtual Pin ----------
#define VPIN_TINGGIAIR V0
#define VPIN_VOLUME V1
#define VPIN_BUZZER_CONTROL V2
#define VPIN_TINGGI V3
#define VPIN_DIAMETER V4
#define VPIN_POMPA V5          // Virtual Pin baru untuk tombol pompa

// ---------- Variabel State Baru ----------
bool pompaState = false;       // Status pompa (false=OFF, true=ON)
bool alarmState = false;       // Status alarm penuh (false=Aman, true=Penuh)

// Variabel untuk debounce saklar manual
unsigned long lastDebounceTime = 0;
bool lastButtonState = HIGH;   // Kondisi HIGH karena INPUT_PULLUP
const long debounceDelay = 50; // 50 ms

// ---------- Fungsi Sensor ----------
float singleDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, PULSE_TIMEOUT);
  if (duration == 0) return -1;
  float distance = duration * 0.034 / 2.0;
  return distance;
}

float medianOfArray(float arr[], int n) {
  for (int i = 1; i < n; i++) {
    float key = arr[i];
    int j = i - 1;
    while (j >= 0 && arr[j] > key) {
      arr[j + 1] = arr[j];
      j--;
    }
    arr[j + 1] = key;
  }
  return arr[n / 2];
}

float getFilteredDistance() {
  float samples[SAMPLE_COUNT];
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    float d = singleDistance();
    if (d <= 0) {
      if (lastValidDistance > 0) d = lastValidDistance;
      else d = 9999;
    } else {
      lastValidDistance = d;
    }
    samples[i] = d;
    delay(10);
  }

  float med = medianOfArray(samples, SAMPLE_COUNT);
  if (med > 1000) return -1;

  if (smoothedDistance < 0) {
    smoothedDistance = med;
    return smoothedDistance;
  }

  float delta = med - smoothedDistance;
  if (abs(delta) > MAX_DELTA) {
    med = smoothedDistance + (delta > 0 ? MAX_DELTA : -MAX_DELTA);
  }

  smoothedDistance = ALPHA * med + (1.0 - ALPHA) * smoothedDistance;
  return smoothedDistance;
}

// ---------- Blynk Control ----------
BLYNK_WRITE(VPIN_BUZZER_CONTROL) {
  buzzerEnabled = param.asInt();
  if (!buzzerEnabled) {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
  }
}

BLYNK_WRITE(VPIN_TINGGI) {
  tinggiGelas = param.asFloat();
}

BLYNK_WRITE(VPIN_DIAMETER) {
  diameter = param.asFloat();
}

// Kontrol Pompa dari Blynk
BLYNK_WRITE(VPIN_POMPA) {
  int state = param.asInt();
  if (state == 1) {
    if (!alarmState) { // Hanya bisa nyala jika TIDAK penuh
      pompaState = true;
    } else {
      pompaState = false; // Gagal, update tombol di App
      Blynk.virtualWrite(VPIN_POMPA, 0);
    }
  } else {
    pompaState = false;
  }
}

// ---------- Fungsi utama monitoring ----------
float tinggiAir = 0;
float volume = 0;

void hitungDanTampilkan() {
  float distance = getFilteredDistance();

  if (distance > 0 && tinggiGelas > 0 && diameter > 0) {
    tinggiAir = tinggiGelas - distance;
    if (distance >= tinggiGelas) tinggiAir = 0;
    if (tinggiAir < 0) tinggiAir = 0;
    if (tinggiAir > tinggiGelas) tinggiAir = tinggiGelas;

    float r = diameter / 2.0;
    volume = 3.1416 * r * r * tinggiAir;

    // logika alarm
    if (!alarmState && distance < ALARM_ON) alarmState = true;
    else if (alarmState && distance > ALARM_OFF) alarmState = false;

    // --- LOGIKA KONTROL POMPA ---
    // Safety Override: Jika alarm (penuh) aktif, paksa pompa mati
    if (alarmState && pompaState) {
      pompaState = false;
      Blynk.virtualWrite(VPIN_POMPA, 0); // Update status di Blynk
    }

    // Terapkan status ke pin Relay (Relay umumnya Active LOW)
    // LOW = Relay ON (Pompa Nyala)
    // HIGH = Relay OFF (Pompa Mati)
    digitalWrite(RELAY_PIN, pompaState ? LOW : HIGH);

    // --- Logika Buzzer & LED (Alarm Penuh) ---
    if (buzzerEnabled) {
      digitalWrite(BUZZER_PIN, alarmState ? HIGH : LOW);
      digitalWrite(LED_PIN, alarmState ? HIGH : LOW);
    }

    // tampilkan di LCD
    if (millis() - lastUpdateLCD >= lcdInterval) {
      lastUpdateLCD = millis();
      lcd.setCursor(0, 0);
      lcd.print("Tinggi: ");
      lcd.print(tinggiAir, 1);
      lcd.print("cm ");
      lcd.setCursor(15, 0); // Pojok kanan atas
      lcd.print(pompaState ? "P" : " "); // Status Pompa (P=Pump)

      lcd.setCursor(0, 1);
      lcd.print("Vol: ");
      lcd.print(volume, 1);
      lcd.print("mL   ");
    }

    Serial.print("Tinggi: ");
    Serial.print(tinggiAir, 1);
    Serial.print(" cm | Volume: ");
    Serial.print(volume, 1);
    Serial.print(" mL | Pompa: ");
    Serial.println(pompaState ? "ON" : "OFF");
  }
}

// ---------- Cek Saklar Manual ----------
void checkManualSaklar() {
  bool reading = digitalRead(SAKLAR_PIN);

  // Cek jika kondisi tombol berubah
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  // Jika kondisi tombol sudah stabil (lolos debounce)
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // Cek jika tombol baru saja ditekan (transisi dari HIGH ke LOW)
    if (reading == LOW && lastButtonState == HIGH) {
      
      if (!pompaState) { // Jika pompa sedang MATI
        if (!alarmState) { // Dan tidak sedang penuh
          pompaState = true; // NYALAKAN pompa
        }
      } else { // Jika pompa sedang NYALA
        pompaState = false; // MATIKAN pompa
      }
      
      // Update status ke Blynk
      Blynk.virtualWrite(VPIN_POMPA, pompaState ? 1 : 0);
    }
  }
  
  lastButtonState = reading;
}


// ---------- Kirim ke Blynk ----------
void kirimKeBlynk() {
  Blynk.virtualWrite(VPIN_TINGGIAIR, tinggiAir);
  Blynk.virtualWrite(VPIN_VOLUME, volume);
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  // Pin Lama
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  // Pin Baru
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(SAKLAR_PIN, INPUT_PULLUP); // Tombol manual pakai PULLUP internal

  // Inisialisasi state awal
  digitalWrite(RELAY_PIN, HIGH); // Pompa MATI (karena Active LOW)
  pompaState = false;

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Menghubungkan...");

  // koneksi ke WiFi dan Blynk menggunakan credential dari credential.h
  Blynk.begin(BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASS);

  lcd.clear();
  lcd.print("Monitoring Air");
  delay(1000);
  lcd.clear();

  timer.setInterval(200L, hitungDanTampilkan); // Cek sensor & logika
  timer.setInterval(1000L, kirimKeBlynk);      // Kirim data ke Blynk
}

// ---------- Loop ----------
void loop() {
  Blynk.run();
  timer.run();
  checkManualSaklar(); // Selalu cek tombol manual di loop utama
}
