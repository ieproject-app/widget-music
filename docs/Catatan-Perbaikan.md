# Catatan Perbaikan Widget Music

Terakhir diperbarui: 1 Juni 2026

## Runtime Deskband

- Widget tetap memakai DeskBand resmi Windows 10 x64, tanpa injection atau overlay taskbar.
- Mode awal compact `132x40`; mode full `300x40` dipilih lewat menu klik kanan.
- Mode full memakai progress-first: teks waktu dan seek bar indikator diperbarui bersama setiap detik.
- Seek bar sengaja display-only. Thumb hover dan jalur marquee lama telah dihapus.
- Background mengambil median sampel surface taskbar terlebih dahulu; warna DWM hanya fallback.
- Popup judul native diposisikan di monitor aktif, memilih sisi atas/bawah sesuai ruang layar.
- Tooltip tombol dan popup judul diaudit sebagai dua perilaku terpisah.

## Keyboard dan Aksesibilitas

- Window deskband dapat menerima fokus keyboard.
- `Left` dan `Right` memindahkan fokus tombol; `Enter` dan `Space` mengeksekusi tombol terpilih.
- Focus ring menggunakan `DrawFocusRect`, termasuk saat high contrast aktif.
- `WM_GETOBJECT` menyediakan provider MSAA dengan tiga child virtual: `Previous`, `Play/Pause`, dan `Next`.
- Provider mengirim `NotifyWinEvent` saat fokus atau state tombol berubah. UI Automation dapat memakai bridge MSAA.

## Host dan IPC

- Named pipe memakai endpoint per sesi Windows: `WidgetMusic.Pipe.v1.Session.<id>`.
- Pipe mempertahankan `PIPE_REJECT_REMOTE_CLIENTS`, ACL logon SID, dan gagal tertutup jika ACL tidak dapat dibuat.
- Deskband wajib menerima handshake `hello.version == 1` sebelum memproses state.
- Metadata dan payload IPC dibatasi agar tidak melampaui buffer.
- Snapshot dan penggantian media session dilindungi mutex khusus.
- Queue command dikosongkan saat teardown.
- Log deskband dan host dirotasi setelah melewati `512 KB`.

## Paket dan Operasional

- DLL serta EXE membawa metadata versi `1.0.0.0`.
- Paket runtime membawa `VERSION.txt` dan `SHA256SUMS.txt`.
- Verifier membandingkan hash DLL/EXE build terhadap paket sehingga distribusi stale gagal terdeteksi.
- Register, install, unregister, dan uninstall memakai helper restart Explorer bersama yang hanya menyentuh sesi pemanggil.
- `.gitattributes` menormalkan line ending lintas lingkungan.

## Pengujian

- `scripts\Build.cmd Debug`
- `scripts\Build.cmd Release`
- `scripts\Run-WidgetMusicTests.cmd Release`
- `scripts\Package-WidgetMusic.cmd Release`
- `scripts\Verify-WidgetMusicGoal.ps1 Release`
- Workflow `.github\workflows\windows-ci.yml` menjalankan alur tersebut pada Windows.

## Catatan Stabilitas

- Akses media berat tetap berada di host, bukan di proses Explorer.
- Empat dokumen eksperimen lama dipertahankan sebagai kandidat arsip; lihat `docs\Audit-Final-1-Juni-2026.md`.
