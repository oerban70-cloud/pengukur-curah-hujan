# Pengukur Curah Hujan — ESP32 + Tipping Bucket + Reed Switch + LCD

Hujan semalam turun berapa milimeter, ada yang tahu? Alat kecil ini yang
mengukurnya: air hujan ditampung corong, dialirkan ke ember jungkit
(tipping bucket) yang jungkit tiap kali penuh. Sebuah magnet di ember
menutup reed switch di rangka setiap jungkitan, ESP32 menghitungnya lewat
interupsi, dan total hujan serta lajunya tampil di LCD — lengkap dengan LED
yang berkedip tiap jungkitan.

Video penjelasan (60 detik):
link menyusul setelah upload

## Komponen

| Bagian | Keterangan |
|---|---|
| ESP32 DevKit | Otak alat |
| Corong penampung 10 x 10 cm | Menampung air hujan ke ember jungkit |
| Ember jungkit (tipping bucket) | Jungkit tiap terisi penuh, magnet menempel di ember |
| Reed switch | Saklar magnetik, menutup tiap kali magnet lewat |
| LCD 16x2 I2C (0x27) | Tampil total hujan (mm) dan laju hujan (mm/jam) |
| LED hijau + resistor 220 Ω | Berkedip 200 ms tiap jungkitan |
| Catu USB 5 V | Adaptor USB / powerbank untuk ESP32 |

## Wiring

| Dari | Ke |
|---|---|
| Reed switch K1 | ESP32 GPIO4 (interupsi, INPUT_PULLUP, aktif LOW) |
| Reed switch K2 | GND |
| LED anoda | ESP32 GPIO2 (katoda lewat resistor 220 Ω ke GND) |
| LCD SDA | ESP32 GPIO21 |
| LCD SCL | ESP32 GPIO22 |
| LCD VCC | ESP32 3V3 |
| LCD GND | GND |
| ESP32 VIN 5V | Catu USB 5 V |
| Semua GND | Digabung |

Skema lengkap: [schematic.png](schematic.png)

## Cara kerja

Setiap jungkitan ember berarti 0,2 mm hujan — itu konstanta kalibrasi
desain alat ini, bukan angka datasheet. Asumsinya: corong penampung
berukuran 10 x 10 cm (luas 100 cm2), sehingga 0,2 mm hujan sama dengan
2 mL air per jungkitan.

Reed switch menutup sesaat tiap kali magnet di ember melewatinya.
Penutupan itu terdeteksi ESP32 lewat interupsi di GPIO4 (tepi turun,
karena pin ditarik ke atas oleh pull-up internal dan reed switch
menariknya ke GND saat menutup). Debounce 150 ms di dalam interupsi
mengabaikan pantulan kontak reed switch, supaya satu jungkitan hanya
dihitung sekali.

Curah hujan dihitung dengan rumus:

```
total hujan (mm) = jumlah jungkitan x 0,2
```

Laju hujan (mm/jam) dihitung dari jumlah jungkitan dalam 60 menit
terakhir (jendela geser) dikali 0,2 mm. Kedua angka tampil di LCD tiap
1 detik dan dikirim juga ke Serial Monitor.

## Kalibrasi (wajib sebelum dipakai)

Ukuran ember tiap orang bisa berbeda, jadi konstanta 0,2 mm per
jungkitan harus diperiksa:

1. Tuang air terukur lewat corong — mis. 100 mL.
2. Hitung jungkitan yang tercatat di Serial Monitor.
3. 100 mL lewat corong 10 x 10 cm seharusnya menghasilkan 50 jungkitan
   (100 mL / 2 mL per jungkitan).
4. Bila hasilnya berbeda, ubah konstanta `MM_PER_TIP` di sketch:
   `MM_PER_TIP = volume tuang (mL) / (jumlah jungkitan x 10)`.

Contoh: 100 mL menghasilkan 40 jungkitan, maka
`MM_PER_TIP = 100 / (40 x 10) = 0,25`.

## Angka compile terverifikasi

Sketch `PengukurCurahHujanReedSwitch.ino` terverifikasi compile pada
25 Nov 2026 dengan arduino-cli 1.6.0, ESP32 core 4.0.0:

- Ukuran sketch: 318.837 byte (24% dari program storage)
- RAM (global variables): 25.608 byte (7% dari dynamic memory)

Library yang dipakai: LiquidCrystal_I2C 1.1.2 (tersedia di `libraries/`).

## Lisensi

MIT — lihat [LICENSE](LICENSE).
