# Changelog

Semua perubahan penting pada Falkon-Rust dicatat di file ini.

Format mengikuti [Keep a Changelog](https://keepachangelog.com/id/1.1.0/).

## [0.2.0] - 2026-11-17

### Ditambahkan

- **Pembaruan daftar filter adblock**: EasyList, EasyPrivacy, daftar uBlock
  Origin (filters, unbreak, badware, privacy), dan ABPindo (iklan Indonesia)
  kini bisa diunduh dan disimpan di `filter_lists/` di direktori konfigurasi,
  lalu dimuat otomatis saat startup. Pembaruan berkala berjalan di latar
  belakang tanpa memblokir dibukanya jendela browser. Tanpa daftar yang
  diunduh, browser tetap berjalan dengan rule bawaan offline. Daftar
  `ublock-badware` dan `ublock-privacy` bersifat opt-in (tidak aktif bawaan)
  agar mesin tetap ringan.
- CLI baru: `--update-filters` (unduh semua daftar aktif lalu keluar),
  `--filter-status` (tampilkan status cache filter), `--no-restore`
  (lewati pemulihan sesi untuk satu kali jalan), dan `--version`.
- **Pemulihan sesi (session restore)**: tab non-privat yang terbuka disimpan
  ke `session.json` dan dipulihkan saat browser dibuka kembali. Bisa
  dimatikan lewat `restore_session: false` di `config.json` atau dari halaman
  Pengaturan. Tab privat tidak pernah disimpan.
- **Allowlist adblock per domain** (`adblock_allowlist` di `config.json` dan
  dari halaman Pengaturan): request dari domain yang masuk allowlist tidak
  diblokir dan CSS kosmetik tidak diinjeksikan.
- Pengaturan hemat RAM yang bisa diubah: `suspend_enabled`,
  `suspend_after_seconds`, `suspend_many_tabs_after_seconds`,
  `suspend_many_tabs_threshold`, dan `http_cache_size_mb`, semuanya bisa
  diubah dari halaman Pengaturan (tab Umum & Privasi) tanpa mengedit JSON.
- Opsi `renderer_process_limit` di `config.json`: membatasi jumlah proses
  renderer Chromium lewat flag `--renderer-process-limit` (0 = default Qt).
  Berguna di perangkat RAM kecil dengan banyak tab; bersifat opt-in.
- `LICENSE` (GPL-3.0), file ini (`CHANGELOG.md`), metadata paket di `Cargo.toml`,
  dan Dependabot untuk update dependensi Cargo dan GitHub Actions.
- Job `test` di CI: `cargo fmt --check`, `cargo clippy`, dan `cargo test`
  di Linux. Job build/rilis kini menunggu job test selesai.
- Test untuk allowlist, cache CSS kosmetik, updater filter, pemulihan sesi,
  dan kompatibilitas `config.json` lama.

### Diubah

- Versi paket diselaraskan ke `0.2.0` (sebelumnya `Cargo.toml` masih `0.1.0`
  sementara tag rilis sudah `v0.1.7`).
- Mesin adblock kini menghitung jumlah rule aktif dan memakai cache CSS
  kosmetik berbatas (maks. 64 entri) agar halaman yang sering dibuka kembali
  tidak menghitung ulang selector setiap kali.
- Seluruh kode Rust diformat dengan `cargo fmt` agar konsisten.

### Diperbaiki

- Rule bawaan `|| sentry.io/api/*` yang penulisannya salah (ada spasi setelah
  `||`) sehingga tidak pernah cocok; kini `||sentry.io/api/*`.
- Fungsi FFI Rust yang menerima raw pointer kini ditandai `unsafe` dengan
  dokumentasi `# Safety`, sehingga `cargo clippy` lolos di toolchain stable
  terbaru (sebelumnya 22 error `not_unsafe_ptr_arg_deref`).
- `run_smart_memory_cleanup` kini memeriksa `view`/`page` null sebelum
  mengaksesnya.
- Daftar filter bawaan kini didokumentasikan dengan jujur: rule bawaan
  offline hanya ~70 rule inti; pemblokiran agresif penuh membutuhkan daftar
  yang diunduh (`--update-filters` atau tombol di Pengaturan).

## [0.1.0] - 2026-11-15

- Rilis awal Falkon-Rust: browser minimalis Rust + Qt6 WebEngine dengan
  mesin adblock, Smart Memory suspension, mode penyamaran, 6 preset tema,
  omnibox dengan bang shortcut, bookmark, riwayat, dan pengelola unduhan.

[0.2.0]: https://github.com/De-violet/web-browser/compare/v0.1.7...v0.2.0
[0.1.0]: https://github.com/De-violet/web-browser/releases/tag/v0.1.0
