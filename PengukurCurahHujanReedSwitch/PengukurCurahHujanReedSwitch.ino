/*
  Pengukur Curah Hujan dengan Tipping Bucket + Reed Switch — ESP32
  ================================================================
  Cara kerja:
  Air hujan ditampung corong lalu dialirkan ke ember jungkit
  (tipping bucket) di bawahnya. Setiap ember terisi penuh, ember
  jungkit dan menumpahkan airnya; sebuah magnet kecil yang menempel
  di ember melewati reed switch di rangka, sehingga reed switch
  menutup sesaat. ESP32 menghitung tiap penutupan itu lewat interupsi
  (dengan debounce), lalu menghitung curah hujan dan menampilkannya
  di LCD I2C 16x2, lengkap dengan LED yang berkedip tiap jungkitan.

  Konstanta kalibrasi (DESAIN, bukan angka datasheet):
    - Tiap jungkitan = 0,2 mm hujan (MM_PER_TIP).
    - Asumsi corong penampung 10 x 10 cm (luas 100 cm2), sehingga
      0,2 mm hujan = 2 mL air per jungkitan.
    - KALIBRASI WAJIB: tuang air terukur lewat corong (mis. 100 mL
      harus menghasilkan 50 jungkitan), lalu sesuaikan MM_PER_TIP
      bila hasilnya berbeda — ukuran ember tiap orang bisa berbeda.

  Laju hujan (mm/jam): dihitung dari jumlah jungkitan dalam 60 menit
  terakhir (jendela geser), dikali MM_PER_TIP.

  Wiring:
    - Reed switch satu kaki -> GPIO4, kaki lain -> GND.
      Dipakai INPUT_PULLUP internal, jadi reed switch AKTIF LOW
      (menutup = pin terbaca LOW). TIDAK perlu resistor eksternal.
    - LED indikator -> GPIO2 (resistor 220 ohm seri ke GND).
    - LCD I2C 16x2 alamat 0x27: SDA -> GPIO21, SCL -> GPIO22,
      VCC -> 3V3, GND -> GND.

  Video penjelasan (60 detik): link menyusul setelah upload
  Repo: https://github.com/oerban70-cloud/pengukur-curah-hujan
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------- Konfigurasi pin ----------
const uint8_t PIN_REED_SWITCH = 4;   // reed switch -> GPIO4 (interupsi, aktif LOW)
const uint8_t PIN_LED_TIP      = 2;   // LED berkedip tiap jungkitan
const uint8_t PIN_SDA_LCD      = 21;  // SDA bus I2C
const uint8_t PIN_SCL_LCD      = 22;  // SCL bus I2C

// ---------- Konfigurasi LCD ----------
const uint8_t ALAMAT_LCD       = 0x27;  // alamat I2C LCD 16x2
const uint8_t KOLOM_LCD        = 16;    // lebar layar (karakter)
const uint8_t BARIS_LCD        = 2;     // tinggi layar (baris)

// ---------- Konfigurasi kalibrasi dan laju hujan ----------
// Tiap jungkitan ember = 0,2 mm hujan (kalibrasi desain, lihat atas).
const float MM_PER_TIP          = 0.2;
// Debounce: abaikan penutupan reed switch berikutnya selama 150 ms
// (ember butuh waktu untuk jungkit penuh; pantulan kontak jauh lebih cepat).
const unsigned long DEBOUNCE_TIP_MS = 150;
// Jendela laju hujan: 60 menit terakhir.
const unsigned long JENDELA_LAJU_MS = 60UL * 60UL * 1000UL;
// Simpan waktu (millis) jungkitan terakhir, cukup untuk 120 jungkitan
// dalam satu jam (24 mm/jam — hujan sangat lebat).
const uint8_t MAKS_TIP_JENDELA  = 120;
const unsigned long PERIODE_TAMPIL_MS = 1000;  // tampilkan hasil tiap 1 detik
const unsigned long DURASI_LED_MS     = 200;   // LED menyala 200 ms tiap jungkitan
const unsigned long BAUD_SERIAL       = 115200;

// ---------- State (volatile = diubah di dalam interupsi) ----------
volatile uint32_t jumlahTip      = 0;    // total jungkitan sejak dinyalakan
volatile unsigned long waktuTipTerakhirMs = 0;  // millis jungkitan terakhir
volatile bool adaTipBaru         = false;  // penanda untuk loop()

// Riwayat waktu jungkitan (jendela geser 60 menit) — diisi dari loop().
unsigned long riwayatTipMs[MAKS_TIP_JENDELA];
uint8_t jumlahRiwayat = 0;
uint8_t indeksRiwayat = 0;

float curahHujanMm     = 0.0;  // total hujan (mm)
float lajuHujanMmPerJam = 0.0; // laju hujan (mm/jam)
unsigned long waktuTampilTerakhirMs = 0;
unsigned long waktuLedMatiMs = 0;

LiquidCrystal_I2C lcd(ALAMAT_LCD, KOLOM_LCD, BARIS_LCD);

// Interupsi reed switch: berjalan tiap kali reed switch menutup (FALLING).
// Hanya menghitung bila sudah lewat masa debounce sejak jungkitan terakhir.
void IRAM_ATTR saatReedMenutup() {
  unsigned long sekarang = millis();
  if (sekarang - waktuTipTerakhirMs >= DEBOUNCE_TIP_MS) {
    waktuTipTerakhirMs = sekarang;
    jumlahTip++;
    adaTipBaru = true;
  }
}

// Masukkan waktu jungkitan ke riwayat jendela geser (ring buffer).
void catatTipKeRiwayat(unsigned long waktuMs) {
  riwayatTipMs[indeksRiwayat] = waktuMs;
  indeksRiwayat = (indeksRiwayat + 1) % MAKS_TIP_JENDELA;
  if (jumlahRiwayat < MAKS_TIP_JENDELA) {
    jumlahRiwayat++;
  }
}

// Hitung laju hujan: jungkitan dalam 60 menit terakhir x mm per jungkitan.
float hitungLajuHujan(unsigned long sekarang) {
  uint8_t tipDalamJendela = 0;
  for (uint8_t i = 0; i < jumlahRiwayat; i++) {
    if (sekarang - riwayatTipMs[i] < JENDELA_LAJU_MS) {
      tipDalamJendela++;
    }
  }
  return tipDalamJendela * MM_PER_TIP;
}

// Tampilkan total hujan dan laju hujan di LCD.
void tampilkanLcd() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Hujan: ");
  lcd.print(curahHujanMm, 1);
  lcd.print(" mm");
  lcd.setCursor(0, 1);
  lcd.print("Laju: ");
  lcd.print(lajuHujanMmPerJam, 1);
  lcd.print(" mm/jam");
}

void setup() {
  Serial.begin(BAUD_SERIAL);
  pinMode(PIN_REED_SWITCH, INPUT_PULLUP);
  pinMode(PIN_LED_TIP, OUTPUT);
  digitalWrite(PIN_LED_TIP, LOW);

  // Interupsi di tepi turun: reed switch menutup = pin jadi LOW.
  attachInterrupt(digitalPinToInterrupt(PIN_REED_SWITCH),
                  saatReedMenutup, FALLING);

  Wire.begin(PIN_SDA_LCD, PIN_SCL_LCD);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Pengukur Hujan");
  lcd.setCursor(0, 1);
  lcd.print("Siap mengukur...");
  delay(1500);
  tampilkanLcd();
  Serial.println("Pengukur curah hujan siap. 1 tip = 0,2 mm.");
}

void loop() {
  unsigned long sekarang = millis();

  // Tangani jungkitan yang dicatat interupsi.
  if (adaTipBaru) {
    noInterrupts();
    uint32_t totalTip = jumlahTip;
    unsigned long waktuTip = waktuTipTerakhirMs;
    adaTipBaru = false;
    interrupts();
    curahHujanMm = totalTip * MM_PER_TIP;
    catatTipKeRiwayat(waktuTip);
    digitalWrite(PIN_LED_TIP, HIGH);
    waktuLedMatiMs = sekarang + DURASI_LED_MS;
    Serial.print("Tip ke-");
    Serial.print(totalTip);
    Serial.print("  total: ");
    Serial.print(curahHujanMm, 1);
    Serial.println(" mm");
  }

  // Matikan LED setelah durasinya habis.
  if (waktuLedMatiMs != 0 && sekarang >= waktuLedMatiMs) {
    digitalWrite(PIN_LED_TIP, LOW);
    waktuLedMatiMs = 0;
  }

  // Perbarui tampilan tiap 1 detik.
  if (sekarang - waktuTampilTerakhirMs >= PERIODE_TAMPIL_MS) {
    waktuTampilTerakhirMs = sekarang;
    lajuHujanMmPerJam = hitungLajuHujan(sekarang);
    tampilkanLcd();
    Serial.print("Total: ");
    Serial.print(curahHujanMm, 1);
    Serial.print(" mm  laju: ");
    Serial.print(lajuHujanMmPerJam, 1);
    Serial.println(" mm/jam");
  }
}
