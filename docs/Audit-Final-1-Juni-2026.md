# Audit Final Widget Music

Tanggal: 1 Juni 2026

## Status Implementasi

Gelombang optimasi final telah diterapkan:

| Area | Status | Ringkasan |
| --- | --- | --- |
| Runtime visual | Selesai | Progress text dan seek bar repaint bersama; surface taskbar disampling lebih dahulu; thumb hover dan marquee lama dihapus; popup judul di-clamp ke monitor aktif. |
| Host dan IPC | Selesai | Mutex session khusus, queue teardown, endpoint pipe per sesi, ACL logon SID fail-closed, remote client reject, handshake versi wajib, batas payload/metadata, rotasi log. |
| Aksesibilitas | Selesai | Keyboard navigation, activation key, focus ring, provider MSAA `WM_GETOBJECT`, tiga child virtual, dan `NotifyWinEvent`. |
| Packaging | Selesai | Hash build-versus-dist, `SHA256SUMS.txt`, `VERSION.txt`, metadata versi biner `1.0.0.0`, helper restart Explorer per sesi, `.gitattributes`. |
| Otomasi | Selesai | Console test ringan dan workflow Windows CI untuk build Debug/Release, tests, package, serta verifier. |

## Verifikasi Wajib

```powershell
.\scripts\Build.cmd Debug
.\scripts\Run-WidgetMusicTests.cmd Debug
.\scripts\Build.cmd Release
.\scripts\Run-WidgetMusicTests.cmd Release
.\scripts\Package-WidgetMusic.cmd Release
.\scripts\Verify-WidgetMusicGoal.ps1 Release
```

## Pemeriksaan Visual Manual

- Compact/full toggle: `132x40 -> 300x40 -> 132x40`.
- Popup judul diuji pada area non-tombol.
- Tooltip tombol diuji terpisah pada tombol play.
- Background dibandingkan dengan area taskbar kosong terdekat; target selisih maksimum kanal RGB `<= 16`.
- Uji light theme, dark theme, high contrast, DPI `100%`, `125%`, `150%`, dan taskbar atas/bawah.
- Uji keyboard `Left`, `Right`, `Enter`, `Space` serta pembacaan screen reader.

## Hasil Runtime Lokal

| Pemeriksaan | Hasil |
| --- | --- |
| Build Debug x64 | Lulus, `0 warning`, `0 error`. |
| Build Release x64 | Lulus, `0 warning`, `0 error`. |
| Console tests Debug dan Release | Lulus. |
| Guard paket stale | Verifier gagal sebelum packaging dan lulus setelah packaging. |
| Paket runtime | Sekitar `282 KB`, tanpa PDB/intermediate. |
| Toggle runtime | `132x40 -> 300x40 -> 132x40`. |
| Background compact | Widget `#26393F`, taskbar kosong terdekat `#2C3C42`, selisih kanal maksimum `6`. |
| CPU idle 8 detik | `explorer.exe = 0,0000s`, `WidgetMusicHost.exe = 0,0000s`. |
| Pipe runtime | `WidgetMusic.Pipe.v1.Session.1`. |
| MSAA runtime | Self `Widget Music`, tiga push button virtual: `Previous`, `Play/Pause`, `Next`. |
| Tooltip tombol | Terlihat sebagai `Play / pause` pada inspeksi hover terpisah. |

## Pemeriksaan Manual Tersisa

Lingkungan audit lokal sedang tidak memiliki media aktif. Pemeriksaan berikut tetap perlu dilakukan saat menutup rilis:

- Perubahan teks progress dan seek bar setiap detik saat lagu berjalan.
- Popup judul saat track berganti.
- Light theme, dark theme, high contrast, DPI `100%`, `125%`, `150%`, dan taskbar atas.
- Operasi keyboard nyata serta pembacaan screen reader interaktif.
- Dua sesi Windows aktif sekaligus dan uninstall nyata pada sesi non-utama.

## Kandidat Arsip

Empat dokumen eksperimen lama dipertahankan tanpa penghapusan otomatis karena beberapa klaimnya sudah stale:

- `docs\Analisis-Peningkatan-Widget-Music.md`
- `docs\Changelog-Perbaikan-31-Mei-2026.md`
- `docs\Git-Rollback-Safety-Check.md`
- `docs\Rencana-Perbaikan-Urgent.md`

## Batasan

- DeskBand tetap menargetkan Windows 10 x64.
- Seek bar tetap indikator, bukan kontrol input.
- Git history menjadi sumber pemulihan jika marquee suatu hari perlu dievaluasi kembali.
