# Falkon-Rust

**Falkon-Rust** adalah peramban web minimalis berkinerja tinggi yang menggabungkan **Rust** (pemblokir iklan & pelacak berbasis *uBlock Origin / EasyList*, manajemen konfigurasi, riwayat asinkron, dan generator antarmuka) dengan **Qt6 WebEngine (C++17)** untuk *rendering* halaman web modern yang hemat memori.

## Fitur Utama

- **Rust Native uBlock Origin Engine (`adblock` crate):** Memblokir iklan dan pelacak di tingkat *network request interceptor* sebelum menyentuh jaringan, dilengkapi injeksi filter kosmetik CSS otomatis per domain.
- **Smart Memory Suspension:** Secara otomatis menangguhkan (*suspend*) tab latar belakang yang tidak aktif untuk menghemat RAM, namun tetap melindungi tab yang sedang memutar audio/video.
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
| `--check <URL>` | Menguji apakah suatu URL diblokir oleh mesin AdBlock (tanpa membuka GUI) |
| `--homepage <URL>` | Mengganti halaman beranda bawaan untuk sesi ini |
| `--search <TEMPLATE>` | Mengganti URL mesin pencari (gunakan `%s` untuk kueri) |
| `-h`, `--help` | Menampilkan bantuan penggunaan |

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

- `config.json` — Konfigurasi tema, warna, posisi tab bar, zoom, dan preferensi beranda.
- `bookmarks.json` — Daftar situs yang disimpan ke *bookmark*.
- `history.json` — Catatan riwayat penjelajahan.
- `custom_rules.txt` — Aturan filter uBlock / EasyList tambahan milik pengguna.
- `user_chrome.qss` — Stylesheet QSS kustom untuk antarmuka jendela Qt.
- `user_style.css` — CSS kustom yang diinjeksikan ke setiap halaman web.
- `user_script.js` — Userscript JavaScript kustom yang diinjeksikan saat halaman selesai dimuat.
