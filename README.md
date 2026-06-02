# SnipTune 10 (Windows 10 DeskBand + Host)

`SnipTune 10` adalah toolbar/deskband asli untuk Windows 10 dari [SnipGeek](https://snipgeek.com) yang muncul di:

`Right click taskbar > Toolbars > SnipTune 10`

Arsitektur V1:

1. `WidgetMusicDeskband.dll` (in-proc COM DeskBand) hidup di `explorer.exe`, menggambar UI kecil di taskbar dan mengirim perintah tombol.
2. `WidgetMusicHost.exe` (out-of-proc companion) membaca/mengontrol media session via `GlobalSystemMediaTransportControlsSessionManager` dan menjadi server named pipe untuk IPC.

Komunikasi: named pipe lokal per sesi Windows (JSON lines, UTF-8) dengan handshake versi wajib.

## Build (CLI)

Prasyarat:

* Visual Studio Build Tools (Desktop development with C++) - VS 2022 atau lebih baru
* Windows 10 SDK (10.0.x)
* Inno Setup 6, hanya jika ingin membuat installer `.exe`

Build x64 Release:

```bat
.\scripts\Build.cmd Release
```

Output ada di:

`out\Release\x64\WidgetMusicDeskband.dll`  
`out\Release\x64\WidgetMusicHost.exe`

Folder build Release juga berisi PDB dan intermediate file untuk debugging, jadi ukurannya bisa jauh lebih besar dari runtime. Untuk membuat paket runtime bersih:

```bat
.\scripts\Package-WidgetMusic.cmd Release
```

Paket kecil ada di `out\dist\SnipTune10`. Paket membawa DLL, EXE, script runtime, `VERSION.txt`, dan `SHA256SUMS.txt`.

Untuk membuat installer Windows 10 x64:

```bat
.\scripts\Build-Installer.cmd Release
```

Output installer ada di:

`out\dist\SnipTune10Setup-1.0.3-x64.exe`

Script installer juga memeriksa agar binary Release tidak bergantung pada runtime Visual C++ dinamis seperti `MSVCP140.dll` dan `VCRUNTIME140*.dll`.

## Install dari Installer

Jalankan `SnipTune10Setup-1.0.3-x64.exe` di Windows 10 x64. Installer memasang file ke profil pengguna di `%LOCALAPPDATA%\SnipGeek\SnipTune 10`, mendaftarkan DeskBand, mencoba menampilkan toolbar otomatis, lalu me-restart Explorer sebentar agar toolbar dikenali.

Jika toolbar belum terlihat setelah install, aktifkan manual dari:

`Right click taskbar > Toolbars > SnipTune 10`

Uninstall dari Apps & Features atau Control Panel akan unregister DeskBand dan me-restart Explorer sebelum file dihapus.

## Update dari Installer

Untuk update versi berikutnya, naikkan versi aplikasi di resource/installer, build installer baru, lalu jalankan installer `.exe` baru di laptop yang sama. Karena installer memakai AppId yang sama, Inno Setup akan memperbarui instalasi yang sudah ada di `%LOCALAPPDATA%\SnipGeek\SnipTune 10`.

Saat update, installer akan unregister versi lama dan me-restart Explorer terlebih dahulu supaya `WidgetMusicDeskband.dll` tidak terkunci, menimpa file dengan versi baru, lalu register ulang dan mencoba menampilkan toolbar lagi. Untuk distribusi publik, file installer sebaiknya diberi nama sesuai versi, misalnya `SnipTune10Setup-1.0.3-x64.exe`.

Panduan update/release yang lebih lengkap ada di `docs\Panduan-Update-Release.md`. Alur GitHub Release ada di `docs\GitHub-Release-Process.md`.

## Install / Register

Register deskband + restart Explorer (direkomendasikan agar toolbar muncul):

```bat
.\scripts\Register-WidgetMusic.cmd Release restart
```

Opsional, jika ingin script mencoba menampilkan toolbar otomatis:

```bat
.\scripts\Register-WidgetMusic.cmd Release restart auto
```

Lalu aktifkan:

`Right click taskbar > Toolbars > SnipTune 10`

Catatan:
* Default sekarang non-interactive: script tidak auto-enable toolbar kecuali diberi flag `auto`/`enable`.
* Jika pakai mode `auto`, script memakai timeout agar proses tidak macet saat dialog konfirmasi Windows muncul.
* Restart Explorer hanya menyentuh sesi Windows pengguna yang menjalankan script.
* Jika toolbar belum terlihat, aktifkan manual dari menu Toolbars.
* Kadang menu Toolbars perlu dibuka dua kali setelah register.

## Uninstall / Unregister

```bat
.\scripts\Unregister-WidgetMusic.cmd Release restart
```

## Debugging cepat

* Jika toolbar tampil tapi status `Disconnected`, pastikan `WidgetMusicHost.exe` ada di folder output yang sama dengan DLL.
* Host auto-start dari deskband dengan delay sekitar 7 detik saat Explorer baru aktif, lalu reconnect jika host mati.
* Saat toolbar dimatikan, deskband memutus pipe sehingga host ikut berhenti.
* Widget sekarang mulai dari mode compact 132x40; untuk pindah mode gunakan klik kanan pada widget lalu pilih `Compact view` atau `Full view`.
* Peralihan compact/full memakai animasi resize singkat sekitar 200 ms. Jika animasi UI dimatikan dari pengaturan aksesibilitas Windows, pergantian kembali instan.
* Saat mode compact dan lagu berganti, title tampil sebentar sebagai popup native di atas widget agar lebih terbaca.
* Mode full menampilkan progress text dan progress bar display-only. Tidak ada seek melalui widget.
* Tombol bisa dioperasikan dengan keyboard: `Left`, `Right`, `Enter`, dan `Space`.
* Screen reader dapat membaca tiga tombol virtual: `Previous`, `Play/Pause`, dan `Next`.
* Tombol media hanya aktif saat ada target media yang valid; kondisi kosong tidak bisa mengirim play/pause palsu.
* Audit invariant goal bisa dijalankan dengan `.\scripts\Verify-WidgetMusicGoal.ps1 Release`.
* Test ringan bisa dijalankan dengan `.\scripts\Run-WidgetMusicTests.cmd Release`.
* Sampling runtime peralihan compact/full bisa dijalankan dengan `.\scripts\Inspect-WidgetMusicTransition.ps1`.
* Log debug, jika diaktifkan lewat registry, bisa dibaca dengan `.\scripts\Read-WidgetMusicLogs.ps1 -Tail 80`.
