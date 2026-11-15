# Falkon-Rust

Browser modern minimalis berbasis Rust dengan performa tinggi, multi-tab, uBlock Origin adblocker, dual search engine (DuckDuckGo + Google), cosmetic filtering, dan Smart Memory Manager (< 100MB RAM).

## Fitur Utama & Optimasi

### 1. Dual Search Engine (Default: Google & DuckDuckGo)
- **Tombol Logo Vektor di Navbar:** Tombol logo vector compact (Google / DuckDuckGo) berukuran 32px di samping URL bar. Klik untuk beralih instan.
- **Default Search:** Google (`https://www.google.com/search?q=%s`).
- **Shortcut Prefix Pintar di URL Bar:**
  - `!g <query>` atau `g <query>`: Mencari via Google.
  - `!d <query>` atau `d <query>`: Mencari via DuckDuckGo Lite.
  - Query tanpa prefix otomatis menggunakan Google (atau search engine yang aktif).

### 2. Smart Memory Manager (< 100MB RAM & Sleep Tabs)
- **Baseline Ringan (< 100MB):** Menggunakan Chromium low-end device mode, single process renderer, dan memory cache cap 10MB.
- **Sleeping Tabs Otomatis:**
  - Tab latar belakang yang tidak aktif otomatis ditidurkan (`💤 [Title]`) dan memori direlakan ke sistem.
  - Membuka banyak tab (10–50+ tab) tidak akan membuat RAM membengkak.
  - Saat tab diklik kembali, halaman langsung dimuat ulang otomatis secara mulus.
- **Proteksi Streaming Video & Audio:**
  - Tab yang sedang memutar video/audio (YouTube, Twitch, Spotify, dll) dideteksi via `recentlyAudible()` dan **TIDAK PERNAH ditidurkan**.
  - Akselerasi decode video hardware (VA-API) aktif sehingga konsumsi CPU streaming sangat kecil (< 5-10%).

### 3. Start Page Mandiri & Minimalis (`falkon:home`)
- **Built-in Local Start Page:** Tidak lagi menggunakan DuckDuckGo sebagai awalan.
- **Ultra-Minimalist:** Jam digital waktu nyata, logo ringkas, kotak pencarian Google terintegrasi, shortcut situs favorit (GitHub, Hacker News, Wikipedia, dll), dan panduan hotkey.
- **Zero Latency & 0 KB Data:** Dimuat instan 0ms secara lokal tanpa request jaringan luar.

### 4. Dialog Pengaturan & Kustomisasi UI/UX (GUI Dialog)
- **Akses Pengaturan:** Klik tombol menu `≡` di navbar lalu pilih **⚙️ Pengaturan & Kustomisasi...** atau tekan shortcut `Ctrl+,`.
- **Pengaturan yang Dapat Diubah Secara Langsung:**
  - **Tema Tampilan:** Pilihan instan Dark (Catppuccin), Light, Nord, Gruvbox, atau Custom (QSS).
  - **Posisi Tab Bar:** Atas (Top), Bawah (Bottom), atau Sembunyikan (Hidden).
  - **Default Search Engine:** Google / DuckDuckGo.
  - **Halaman Beranda:** Ganti URL awal atau tetap gunakan `falkon:home`.
  - **Visibilitas Komponen:** Toggle tombol navigasi dan badge uBlock.
  - **Zoom Default:** Skala tampilan default.
  - Perubahan langsung diaplikasikan dan otomatis tersimpan ke [~/.config/falkon-rust/config.json](file:///home/deviolete/.config/falkon-rust/config.json).

## Menjalankan

```bash
cd /home/deviolete/Projects/falkon-rust

# Jalankan release binary
./target/release/falkon-rust

# Jalankan test suite
cargo test
```
