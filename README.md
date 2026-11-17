# Falkon-Rust

[![Build and Release](https://github.com/De-violet/web-browser/actions/workflows/build.yml/badge.svg)](https://github.com/De-violet/web-browser/actions/workflows/build.yml)

**Falkon-Rust** adalah peramban web minimalis berkinerja tinggi yang menggabungkan **Rust** (pemblokir iklan & pelacak berbasis *uBlock Origin / EasyList*, manajemen konfigurasi, riwayat asinkron, dan generator antarmuka) dengan **Qt6 WebEngine (C++17)** untuk *rendering* halaman web modern yang hemat memori.

Fokusnya: **RAM tetap rendah meski banyak tab, streaming video lancar, pemblokiran iklan agresif tapi ringan, mudah dipakai, dan kustomisasi yang fleksibel.**

## Fitur Utama

- **Rust Native uBlock Origin Engine (`adblock` crate):** Memblokir iklan dan pelacak di tingkat *network request interceptor* sebelum menyentuh jaringan, dilengkapi injeksi filter kosmetik CSS otomatis per domain. Daftar filter EasyList, EasyPrivacy, uBlock, dan ABPindo bisa diperbarui lewat `--update-filters` atau tombol di Pengaturan, lalu di-cache secara lokal (lihat [Daftar Filter](#daftar-filter--pembaruan)). Ada juga allowlist per domain untuk situs yang ingin dikecualikan.
- **Smart Memory Suspension:** Secara otomatis menangguhkan (*suspend*) tab latar belakang yang tidak aktif untuk menghemat RAM, namun tetap melindungi tab yang sedang memutar audio/video. Ambang waktu, ambang jumlah tab, dan ukuran HTTP cache bisa diubah dari Pengaturan.
- **Pemulihan Sesi (Session Restore):** Tab yang terbuka saat browser ditutup akan dipulihkan saat dibuka kembali. Tab privat tidak pernah disimpan. Nonaktifkan lewat Pengaturan atau jalankan dengan `--no-restore` untuk satu sesi segar.
- **Mode Penyamaran Sejati (Private / Incognito Mode):** Menggunakan `QWebEngineProfile` *off-the-record* terpisah yang tidak menyimpan riwayat kunjungan, *cache* disk, maupun *cookie* persisten (`Ctrl+Shift+N` atau `--private`).
- **6 Preset Tema Modern + Custom QSS:** Tersedia preset *Dark Glass*, *Catppuccin Mocha*, *Nordic Frost*, *Gruvbox Dark*, *Minimal OLED*, dan *Catppuccin Latte (Light)*, lengkap dengan editor token warna dan `user_chrome.qss`.
- **Omnibox Pintar & Pintasan Bang:** Mendukung pencarian langsung dengan awalan `!g` (Google), `!d` (DuckDuckGo), `!gh` (GitHub), `!yt` (YouTube), dan `!w` (Wikipedia).
- **UI/UX Lengkap:**
  - *Inline Find-in-Page Bar* (`Ctrl+F`) dengan navigasi *Next/Previous* dan penghitung kecocokan.
  - Indikator keamanan HTTPS/HTTP di bilah alamat, *favicon* tab, dan *progress bar* pemuatan halaman.
  - Pemulihan tab yang baru ditutup (`Ctrl+Shift+T`) dan menu konteks klik-kanan pada tab (Duplikat, Suspensi, Tutup Tab Lainnya).
  - Pengelola Unduhan (`Ctrl+J`), Bookmark (`Ctrl+B` / `Ctrl+D`) dengan kotak pencarian, dan Riwayat (`Ctrl+H`) dengan pencarian instan.
  - Dukungan penuh untuk tautan `target="_blank"`, jendela *popup*, *fullscreen* video HTML5, dan dialog izin perangkat (kamera, mikrofon, lokasi, notifikasi).

---

## Prasyarat Kompilasi

Pastikan perangkat Anda memiliki **Rust Toolchain** (`cargo`, `rustc`) serta pustaka pengembangan **Qt6 WebEngine** dan **Qt6 SVG**:

### Arch Linux / Manjaro
```bash
sudo pacman -S base-devel rust pkgconf qt6-base qt6-webengine qt6-svg
```

### Ubuntu / Debian / Linux Mint
```bash
sudo apt update
sudo apt install build-essential cargo rustc pkg-config qt6-base-dev qt6-webengine-dev libqt6svg6-dev
```

### Fedora
```bash
sudo dnf install gcc-c++ cargo rust pkgconf-pkg-config qt6-qtbase-devel qt6-qtwebengine-devel qt6-qtsvg-devel
```

---

## Cara Membangun & Menjalankan

```bash
# 1. Build dalam mode rilis (optimal)
cargo build --release

# 2. Jalankan aplikasi
./target/release/falkon-rust

# 3. Jalankan unit & integration tests
cargo test
```

### Opsi Baris Perintah (CLI)

| Opsi | Deskripsi |
| :--- | :--- |
| `falkon-rust [URL]` | Membuka URL tertentu saat startup |
| `--private` / `--incognito` | Membuka tab awal dalam Mode Penyamaran |
| `--rules <FILE>` | Memuat file aturan filter EasyList / uBlock tambahan |
| `--update-filters` | Mengunduh daftar filter aktif, menyimpannya ke cache, lalu keluar (tanpa GUI) |
| `--filter-status` | Menampilkan status cache daftar filter (tanpa GUI) |
| `--check <URL>` | Menguji apakah suatu URL diblokir oleh mesin AdBlock (tanpa membuka GUI) |
| `--no-restore` | Melewati pemulihan sesi untuk sesi ini |
| `--version` | Menampilkan versi aplikasi |
| `--homepage <URL>` | Mengganti halaman beranda bawaan untuk sesi ini |
| `--search <TEMPLATE>` | Mengganti URL mesin pencari (gunakan `%s` untuk kueri) |
| `-h`, `--help` | Menampilkan bantuan penggunaan |

---

## Daftar Filter & Pembaruan

Secara bawaan, browser memakai ~70 rule inti offline sehingga tetap memblokir pelacak umum tanpa internet. Untuk pemblokiran agresif penuh, unduh daftar filter lengkap:

```bash
./target/release/falkon-rust --update-filters
# atau dari Pengaturan (Ctrl+,) → Privasi & AdBlock → Perbarui Daftar Filter
```

Daftar yang dipakai (semua bisa diaktifkan/nonaktifkan lewat `filter_lists` di `config.json`):

| Daftar | Isi |
| :--- | :--- |
| `easylist` | Iklan umum (EasyList) |
| `easyprivacy` | Pelacak & privasi (EasyPrivacy) |
| `ublock-filters` | Filter uBlock Origin |
| `ublock-unbreak` | Perbaikan situs yang rusak akibat filter |
| `ublock-badware` | Domain malware/badware (opt-in, tidak aktif bawaan) |
| `ublock-privacy` | Filter privasi uBlock Origin (opt-in, tidak aktif bawaan) |
| `abpindo` | Iklan berbahasa Indonesia (ABPindo) |

Daftar yang diunduh disimpan di `filter_lists/` di direktori konfigurasi dan dimuat ulang otomatis saat startup. Jika `filter_auto_update` aktif (bawaan), daftar yang sudah kedaluwarsa (bawaan 24 jam, lihat `filter_update_interval_hours`) diperbarui di latar belakang tanpa memblokir startup; jika unduhan gagal, cache lama tetap dipakai.

Pengunduhan memakai `curl` (atau `wget` sebagai cadangan; di Windows `curl.exe` lalu PowerShell), tanpa dependensi HTTP tambahan agar biner tetap ringan. Catatan: rule berbasis scriptlet (`##+js(...)`) dan `$redirect`/`$replace` dari daftar uBlock tidak dijalankan oleh mesin `adblock` — yang dipakai adalah pemblokiran jaringan dan filter kosmetik CSS.

## Hemat RAM

- **Suspend tab:** tab latar yang tidak aktif lebih dari `suspend_after_seconds` (bawaan 120 detik; 45 detik jika tab terbuka melebihi `suspend_many_tabs_threshold`) dimuat ulang ke `about:blank` hingga dibuka kembali. Tab yang sedang berbunyi tidak pernah disuspensi, sehingga streaming video/musik aman. Matikan lewat `suspend_enabled: false` atau dari Pengaturan.
- **HTTP cache memori:** dibatasi `http_cache_size_mb` (bawaan 20 MB).
- **Batas proses renderer:** setel `renderer_process_limit` di `config.json` (mis. `4`) untuk membatasi proses renderer Chromium di perangkat RAM kecil. Nilai `0` (bawaan) memakai default Qt. Perubahan berlaku saat browser dibuka kembali.

---

## Pintasan Keyboard

| Pintasan | Fungsi |
| :--- | :--- |
| `Ctrl + T` | Buka Tab Baru |
| `Ctrl + Shift + N` | Buka Tab Penyamaran (Private Mode) |
| `Ctrl + W` | Tutup Tab Aktif |
| `Ctrl + Shift + T` | Pulihkan Tab yang Baru Ditutup |
| `Ctrl + Tab` / `Ctrl + Shift + Tab` | Berpindah Antar Tab |
| `Ctrl + L` | Fokus ke Bilah Alamat (Address Bar) |
| `Ctrl + F` | Buka Bilah Pencarian di Halaman (Find Bar) |
| `Ctrl + D` | Tambah / Hapus Halaman dari Bookmark |
| `Ctrl + B` | Buka Daftar Bookmark |
| `Ctrl + H` | Buka Riwayat Penjelajahan |
| `Ctrl + J` | Buka Pengelola Unduhan |
| `Ctrl + ,` | Buka Halaman Pengaturan (`falkon:settings`) |
| `Ctrl + M` | Sembunyikan / Tampilkan Tombol Navigasi (Minimal Mode) |
| `Ctrl + +` / `Ctrl + -` / `Ctrl + 0` | Perbesar / Perkecil / Reset Zoom Halaman |
| `F11` | Mode Layar Penuh (Fullscreen) |

---

## Struktur Direktori Konfigurasi

Seluruh konfigurasi pengguna disimpan secara otomatis di `~/.config/falkon-rust/` (atau `$XDG_CONFIG_HOME/falkon-rust/`):

- `config.json` — Konfigurasi tema, warna, posisi tab bar, zoom, preferensi beranda, filter, sesi, dan hemat RAM.
- `bookmarks.json` — Daftar situs yang disimpan ke *bookmark*.
- `history.json` — Catatan riwayat penjelajahan.
- `session.json` — Tab yang dipulihkan saat startup (tidak pernah berisi tab privat).
- `filter_lists/` — Cache daftar filter yang diunduh (`*.txt`) dan `status.json`.
- `custom_rules.txt` — Aturan filter uBlock / EasyList tambahan milik pengguna.
- `user_chrome.qss` — Stylesheet QSS kustom untuk antarmuka jendela Qt.
- `user_style.css` — CSS kustom yang diinjeksikan ke setiap halaman web.
- `user_script.js` — Userscript JavaScript kustom yang diinjeksikan saat halaman selesai dimuat.

Semua kunci di `config.json` bersifat opsional dan punya nilai bawaan, sehingga file konfigurasi dari versi lama tetap bisa dibaca.

---

## Pemecahan Masalah

- **`Qt6WebEngineWidgets must be installed` saat build:** instal paket Qt6 WebEngine sesuai distro di atas, lalu jalankan ulang `cargo build --release`.
- **Filter tidak terunduh (`--update-filters` gagal):** pastikan `curl` atau `wget` terinstal dan koneksi internet tersedia. Browser tetap berjalan dengan rule bawaan offline.
- **Situs rusak / video tidak bisa diputar:** coba matikan adblock untuk situs itu dengan menambahkannya ke allowlist di Pengaturan → Privasi & AdBlock, lalu muat ulang halaman.
- **Tab tersuspensi terlalu cepat:** naikkan `suspend_after_seconds` di Pengaturan → Umum, atau matikan Smart Memory.

---

## Lisensi

Falkon-Rust dilisensikan di bawah [Lisensi GPL-3.0](LICENSE). Daftar filter EasyList, EasyPrivacy, uBlock Origin, dan ABPindo adalah karya pembuatnya masing-masing dengan lisensinya sendiri.
