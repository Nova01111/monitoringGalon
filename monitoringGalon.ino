#define BLYNK_TEMPLATE_ID "TMPL6DiXtwnct"
#define BLYNK_TEMPLATE_NAME "monitoringGalon"
#define BLYNK_AUTH_TOKEN "AjZDYqbcqt1DufxccSTudCocxXrk7-L2"

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

#define TRIG_PIN D5
#define ECHO_PIN D6
#define BUZZER_PIN D4
#define LED_PIN D7

LiquidCrystal_I2C lcd(0x27, 16, 2);
BlynkTimer timer;

// ---------- WiFi ----------
char ssid[] = "Zanira";
char pass[] = "ANIHARTATI123";

// ---------- Parameter default ----------
float tinggiGelas = 6.5;   // cm
float diameter = 5.0;      // cm

const int SAMPLE_COUNT = 5;
const unsigned long PULSE_TIMEOUT = 30000;
const float ALPHA = 0.25;
const float MAX_DELTA = 0.5;
const float ALARM_ON = 3.0;
const float ALARM_OFF = 3.3;

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

// ---------- Fungsi utama monitoring ----------
float tinggiAir = 0;
float volume = 0;
bool alarmState = false;

void hitungDanTampilkan() {
  float distance = getFilteredDistance();

  if (distance > 0 && tinggiGelas > 0 && diameter > 0) {
    tinggiAir = tinggiGelas - distance;
    if (distance >= tinggiGelas) tinggiAir = 0;
    if (tinggiAir < 0) tinggiAir = 0;
    if (tinggiAir > tinggiGelas) tinggiAir = tinggiGelas;

    float r = diameter / 2.0;
    volume = 3.1416 * r * r * tinggiAir;

    // tampilkan di LCD
    if (millis() - lastUpdateLCD >= lcdInterval) {
      lastUpdateLCD = millis();
      lcd.setCursor(0, 0);
      lcd.print("Tinggi: ");
      lcd.print(tinggiAir, 1);
      lcd.print("cm   ");
      lcd.setCursor(0, 1);
      lcd.print("Vol: ");
      lcd.print(volume, 1);
      lcd.print("mL   ");
    }

    // logika alarm
    if (!alarmState && distance < ALARM_ON) alarmState = true;
    else if (alarmState && distance > ALARM_OFF) alarmState = false;

    if (buzzerEnabled) {
      digitalWrite(BUZZER_PIN, alarmState ? HIGH : LOW);
      digitalWrite(LED_PIN, alarmState ? HIGH : LOW);
    }

    Serial.print("Tinggi: ");
    Serial.print(tinggiAir, 1);
    Serial.print(" cm | Volume: ");
    Serial.print(volume, 1);
    Serial.println(" mL");
  }
}

// ---------- Kirim ke Blynk ----------
void kirimKeBlynk() {
  Blynk.virtualWrite(VPIN_TINGGIAIR, tinggiAir);
  Blynk.virtualWrite(VPIN_VOLUME, volume);
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Menghubungkan...");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  lcd.clear();
  lcd.print("Monitoring Air");
  delay(1000);
  lcd.clear();

  timer.setInterval(200L, hitungDanTampilkan);
  timer.setInterval(1000L, kirimKeBlynk);
}

// ---------- Loop ----------
void loop() {
  Blynk.run();
  timer.run();
}
