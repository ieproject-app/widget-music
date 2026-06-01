# Panduan Update dan Release SnipTune 10

Dokumen ini adalah pegangan untuk update SnipTune 10 di masa depan. Isinya sengaja dibuat eksplisit supaya bisa dibaca ulang oleh developer atau AI lain tanpa perlu menebak konteks proyek.

## Ringkasan Arsitektur Distribusi

SnipTune 10 adalah brand publik produk ini di bawah SnipGeek. Nama teknis internal `WidgetMusic*` tetap dipakai untuk DLL, EXE, script, dan beberapa identifier lama agar registrasi COM, AppId installer, dan alur update tetap stabil.

SnipTune 10 bukan aplikasi single portable `.exe`. Komponen utamanya adalah:

* `WidgetMusicDeskband.dll`: COM DeskBand yang dimuat oleh `explorer.exe`.
* `WidgetMusicHost.exe`: companion process untuk baca/kontrol media session.
* Installer Inno Setup: memasang file ke `%LOCALAPPDATA%\SnipGeek\SnipTune 10`, register DLL, restart Explorer, dan unregister saat uninstall/update.

Target resmi saat ini:

* Windows 10 x64.
* Installer per-user, tanpa admin.
* Installer unsigned.
* Windows 11 belum menjadi target karena DeskBand/taskbar toolbar bukan jalur stabil di Windows 11.

## File Penting

Jangan ubah `AppId` di `installer\WidgetMusic.iss`. `AppId` yang sama membuat installer versi baru mengenali instalasi lama sebagai aplikasi yang sama.

File yang biasanya disentuh saat release versi baru:

* `installer\WidgetMusic.iss`
  * `#define MyAppVersion "1.0.1"`
  * `OutputBaseFilename=SnipTune10Setup-{#MyAppVersion}-x64`
  * `PrepareToInstall` unregister versi lama sebelum update agar DLL tidak terkunci Explorer.
* `WidgetMusicDeskband\WidgetMusicDeskband.rc`
  * `FILEVERSION`
  * `PRODUCTVERSION`
  * string `FileVersion`
  * string `ProductVersion`
* `WidgetMusicHost\WidgetMusicHost.rc`
  * `FILEVERSION`
  * `PRODUCTVERSION`
  * string `FileVersion`
  * string `ProductVersion`
* `scripts\Package-WidgetMusic.cmd`
  * baris yang menulis `VERSION.txt`.
* `scripts\Verify-WidgetMusicGoal.ps1`
  * update ekspektasi versi binary jika versi dinaikkan.

Jika protocol IPC berubah dan tidak backward-compatible, cek juga:

* `shared\WidgetMusicProtocol.h`
* validasi handshake di host dan deskband.

## Prasyarat Mesin Build

Wajib:

```bat
Visual Studio Build Tools 2022
Desktop development with C++
Windows 10 SDK
```

Untuk membuat installer `.exe`, install Inno Setup 6:

```bat
winget install --id JRSoftware.InnoSetup -e --source winget
```

Verifikasi Inno Setup:

```bat
where ISCC.exe
```

Jika `ISCC.exe` tidak ada di PATH, `scripts\Build-Installer.cmd` tetap akan mencari lokasi default:

* `%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe`
* `%ProgramFiles%\Inno Setup 6\ISCC.exe`

## Alur Update Kode

1. Ubah kode widget/host sesuai kebutuhan.
2. Jalankan build dan test lokal:

```bat
.\scripts\Build.cmd Release
.\scripts\Run-WidgetMusicTests.cmd Release
```

3. Jika widget sedang aktif dari folder build dan DLL terkunci Explorer, gunakan:

```bat
.\scripts\Register-WidgetMusic.cmd Release restart auto
```

Perintah ini build Release, register ulang DeskBand, restart Explorer hanya di sesi user saat ini, dan mencoba menampilkan toolbar.

4. Cek secara manual di taskbar:

```text
Right click taskbar > Toolbars > SnipTune 10
```

5. Jika perlu cek struktur taskbar:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Inspect-WidgetMusicTaskbar.ps1
```

## Alur Release Installer

Untuk release patch biasa, misalnya dari `1.0.1` ke `1.0.2`:

1. Naikkan versi di file berikut:

```text
installer\WidgetMusic.iss
WidgetMusicDeskband\WidgetMusicDeskband.rc
WidgetMusicHost\WidgetMusicHost.rc
scripts\Package-WidgetMusic.cmd
scripts\Verify-WidgetMusicGoal.ps1
```

2. Build paket runtime:

```bat
.\scripts\Package-WidgetMusic.cmd Release
```

Output:

```text
out\dist\SnipTune10
```

3. Jalankan dependency check:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Check-RuntimeDependencies.ps1
```

Harus lulus tanpa import:

```text
MSVCP140.dll
VCRUNTIME140.dll
VCRUNTIME140_1.dll
```

4. Build installer:

```bat
.\scripts\Build-Installer.cmd Release
```

Output untuk versi `1.0.1`:

```text
out\dist\SnipTune10Setup-1.0.1-x64.exe
```

5. Jalankan verifier:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Verify-WidgetMusicGoal.ps1 Release
```

6. Jalankan `git diff --check`:

```bat
git diff --check
```

## Cara Update di Laptop Pengguna

Update end-user saat ini bersifat manual:

1. User download installer versi baru.
2. User menjalankan `SnipTune10Setup-x.y.z-x64.exe`.
3. Installer mendeteksi instalasi lama karena `AppId` sama.
4. Installer menjalankan `Unregister-WidgetMusic.cmd restart` dari instalasi lama sebelum copy file baru.
5. Explorer restart supaya `WidgetMusicDeskband.dll` lama tidak terkunci.
6. Installer copy file baru ke `%LOCALAPPDATA%\SnipGeek\SnipTune 10`.
7. Installer menjalankan `Register-WidgetMusic.cmd restart auto`.
8. User mengecek taskbar. Jika toolbar belum muncul, aktifkan manual:

```text
Right click taskbar > Toolbars > SnipTune 10
```

Tidak ada auto-update built-in di widget saat ini. Jika ingin auto-update nanti, rancang terpisah dengan minimal:

* release feed atau manifest versi,
* signature/checksum installer,
* mekanisme download,
* prompt user,
* proses restart Explorer yang aman.

## Checklist Release

Sebelum membagikan installer:

* Build Release sukses tanpa error.
* `Run-WidgetMusicTests.cmd Release` lulus.
* `Package-WidgetMusic.cmd Release` sukses.
* `Check-RuntimeDependencies.ps1` lulus.
* `Build-Installer.cmd Release` menghasilkan `.exe`.
* `Verify-WidgetMusicGoal.ps1 Release` lulus.
* `git diff --check` bersih.
* Uji install di Windows 10 x64.
* Uji update dari versi sebelumnya.
* Uji uninstall dari Apps & Features / Control Panel.
* Pastikan toolbar bisa aktif manual jika auto-enable gagal.

## Rilis di GitHub

Repo GitHub hanya menyimpan source, script, konfigurasi, dan dokumentasi. File hasil build tidak di-commit karena sudah diabaikan oleh `.gitignore`.

Untuk tiap versi publik, upload file penting sebagai GitHub Release assets:

* `SnipTune10Setup-<version>-x64.exe`
* `SnipTune10-<version>-runtime.zip`
* `SHA256SUMS.txt`

Workflow `.github\workflows\windows-release.yml` akan membuat draft release otomatis saat tag `v<version>` dipush. Detail alurnya ada di `docs\GitHub-Release-Process.md`.

## Catatan untuk AI Lain

Saat menerima tugas "update widget" atau "build installer", lakukan ini:

1. Baca README dan dokumen ini dulu.
2. Jangan ubah installer `AppId`.
3. Jangan mengubah target Windows 10 x64 kecuali user meminta.
4. Jangan menghapus aksesibilitas keyboard; focus ring keyboard boleh ada, klik mouse tidak perlu meninggalkan ring visual.
5. Kalau build gagal karena DLL/EXE terkunci, itu biasanya karena Explorer/host sedang memakai binary dari `out\Release\x64`.
6. Untuk menerapkan binary dev ke taskbar aktif, pakai `Register-WidgetMusic.cmd Release restart auto`.
7. Untuk release publik, hasil utama adalah installer di `out\dist\SnipTune10Setup-<version>-x64.exe`, bukan folder build developer.
8. Setelah perubahan installer atau build script, update `Verify-WidgetMusicGoal.ps1` agar invariant penting tetap dicek.

## Troubleshooting Singkat

Build gagal dengan `cannot open file WidgetMusicDeskband.dll`:

* Explorer sedang memuat DLL lama.
* Jalankan `.\scripts\Register-WidgetMusic.cmd Release restart auto`, atau unregister/restart Explorer sebelum build.

Installer build gagal karena Inno Setup tidak ditemukan:

* Install Inno Setup 6 dengan winget.
* Pastikan `ISCC.exe` bisa ditemukan.

Toolbar tidak muncul setelah install/update:

* Buka manual dari `Right click taskbar > Toolbars > SnipTune 10`.
* Kadang menu Toolbars perlu dibuka dua kali setelah register.

Status widget `Disconnected`:

* Pastikan `WidgetMusicHost.exe` ada di folder yang sama dengan `WidgetMusicDeskband.dll`.
* Restart Explorer atau register ulang.
