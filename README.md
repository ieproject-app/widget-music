# Widget Music (Windows 10 DeskBand + Host)

`Widget Music` adalah toolbar/deskband asli untuk Windows 10 yang muncul di:

`Right click taskbar > Toolbars > Widget Music`

Arsitektur V1:

1. `WidgetMusicDeskband.dll` (in-proc COM DeskBand) hidup di `explorer.exe`, menggambar UI kecil di taskbar dan mengirim perintah tombol.
2. `WidgetMusicHost.exe` (out-of-proc companion) membaca/mengontrol media session via `GlobalSystemMediaTransportControlsSessionManager` dan menjadi server named pipe untuk IPC.

Komunikasi: named pipe lokal per sesi Windows (JSON lines, UTF-8) dengan handshake versi wajib.

## Build (CLI)

Prasyarat:

* Visual Studio Build Tools (Desktop development with C++) - VS 2022 atau lebih baru
* Windows 10 SDK (10.0.x)

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

Paket kecil ada di `out\dist\WidgetMusic`. Paket membawa DLL, EXE, script runtime, `VERSION.txt`, dan `SHA256SUMS.txt`.

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

`Right click taskbar > Toolbars > Widget Music`

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
* Saat mode compact dan lagu berganti, title tampil sebentar sebagai popup native di atas widget agar lebih terbaca.
* Mode full menampilkan progress text dan progress bar display-only. Tidak ada seek melalui widget.
* Tombol bisa dioperasikan dengan keyboard: `Left`, `Right`, `Enter`, dan `Space`.
* Screen reader dapat membaca tiga tombol virtual: `Previous`, `Play/Pause`, dan `Next`.
* Tombol media hanya aktif saat ada target media yang valid; kondisi kosong tidak bisa mengirim play/pause palsu.
* Audit invariant goal bisa dijalankan dengan `.\scripts\Verify-WidgetMusicGoal.ps1 Release`.
* Test ringan bisa dijalankan dengan `.\scripts\Run-WidgetMusicTests.cmd Release`.
* Log debug, jika diaktifkan lewat registry, bisa dibaca dengan `.\scripts\Read-WidgetMusicLogs.ps1 -Tail 80`.
