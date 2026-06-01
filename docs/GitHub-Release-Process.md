# GitHub Release Process

Dokumen ini menjelaskan susunan GitHub yang rapi untuk SnipTune 10.

## Yang Di-Commit ke Repo

Commit hanya source dan file pendukung yang dibutuhkan untuk membangun aplikasi:

* Source C++ di `WidgetMusicDeskband`, `WidgetMusicHost`, `WidgetMusicTests`, dan `shared`.
* Script build/install/test di `scripts`.
* Konfigurasi installer di `installer`.
* Workflow GitHub Actions di `.github`.
* Dokumentasi di `README.md` dan `docs`.

Jangan commit hasil build:

* `out\`
* installer `.exe`
* runtime `.zip`
* `.pdb`, `.obj`, `.lib`, `.exp`, `.res`
* file lokal Visual Studio seperti `.vs`

Aturan ini sudah dijaga oleh `.gitignore`.

## Yang Di-Upload per Versi

Untuk tiap versi publik, upload file penting saja sebagai GitHub Release assets:

* `SnipTune10Setup-<version>-x64.exe`
  * File utama untuk pengguna biasa.
* `SnipTune10-<version>-runtime.zip`
  * Paket runtime manual untuk debugging atau distribusi tanpa installer.
* `SHA256SUMS.txt`
  * Checksum release assets.

GitHub otomatis menyediakan source archive (`Source code (zip)` dan `Source code (tar.gz)`), jadi tidak perlu upload source zip manual.

## Cara Membuat Release

1. Pastikan versi sudah dinaikkan sesuai `docs\Panduan-Update-Release.md`.
2. Commit perubahan source, script, installer config, dan dokumentasi.
3. Buat tag versi:

```bat
git tag v1.0.1
git push origin v1.0.1
```

4. Workflow `.github\workflows\windows-release.yml` akan berjalan otomatis.
5. Workflow membangun installer, runtime zip, dan checksum.
6. Workflow membuat GitHub Release sebagai draft.
7. Buka halaman Releases di GitHub, cek assets, isi catatan rilis jika perlu, lalu publish.

## Release Manual dari GitHub Actions

Jika ingin menjalankan tanpa tag push, buka tab Actions:

```text
Actions > windows-release > Run workflow
```

Isi `version`, misalnya:

```text
1.0.1
```

Workflow manual tetap membuat draft release dengan tag `v<version>`.

## Checklist sebelum Publish Release

Pastikan assets berikut ada di draft release:

```text
SnipTune10Setup-<version>-x64.exe
SnipTune10-<version>-runtime.zip
SHA256SUMS.txt
```

Cek juga:

* Installer version sesuai tag.
* Workflow test dan verifier lulus.
* `SHA256SUMS.txt` berisi hash untuk installer dan runtime zip.
* Catatan rilis menyebut perubahan penting dan instruksi update singkat.

## Catatan untuk AI Lain

Jika diminta "susun release GitHub" atau "publish versi baru":

1. Jangan commit folder `out`.
2. Jangan upload semua isi build folder.
3. Pastikan release assets hanya installer, runtime zip, dan checksum.
4. Pastikan release workflow tetap draft by default agar manusia bisa mengecek sebelum publik.
5. Jangan ubah `AppId` installer, karena itu membuat update dari versi lama tidak dikenali.
