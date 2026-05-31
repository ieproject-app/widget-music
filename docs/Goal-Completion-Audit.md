# Widget Music Active Goal Audit

Tanggal audit: 31/05/2026

## Ringkasan

Requirement aktif mengganti perilaku collapse otomatis menjadi mode compact/full eksplisit. Fokusnya adalah rasa Windows 10 yang lebih stabil: tidak ada auto-hide animasi, ukuran compact tetap ringan, dan pengguna punya tombol jelas untuk pindah mode.

## Requirement dan Bukti

| Requirement | Status | Bukti |
| --- | --- | --- |
| Collapse otomatis diganti dengan mode compact. | Terbukti via source/build | `BandDisplayMode::Compact`, `kBandCompactWidth = 132`, dan `DesiredBandWidth()` memilih ukuran compact/full. Build Release x64 lolos tanpa warning/error. |
| Ada trigger untuk pindah compact/full tanpa icon tambahan di surface. | Terbukti via source/build | `WM_RBUTTONUP`/`WM_CONTEXTMENU` memanggil `ShowModeContextMenu(...)`, lalu menu `Compact view` / `Full view` menjalankan `SetDisplayMode(...)`. |
| Animasi widget auto-hide dihapus. | Terbukti via source/verifier | Verifier memastikan konstanta dan jalur lama `AutoHide`, `Collapsing`, `Expanding`, `StartCollapse`, dan `drawRevealButton` tidak ada di deskband. |
| Compact mode menampilkan title sementara saat lagu berganti. | Terbukti via source/verifier | `OnStateUpdated()` membandingkan title terbaru dengan `_lastPrimaryText`, lalu memanggil `StartCompactTitleReveal(primary)` selama `kCompactTitleRevealMs = 3200`. |
| Title compact tampil sebagai kartu native di atas widget, bukan mengganti tombol media. | Terbukti via source/verifier | `ShowCompactTitlePopup(...)` memakai tooltip tracked (`TTM_TRACKPOSITION` + `TTM_TRACKACTIVATE`) dan layout compact tetap hanya tombol media. |
| Tombol media tetap tidak bisa memicu play/pause palsu saat tidak ada media. | Terbukti via source/verifier | Deskband mengaktifkan tombol hanya ketika `connected && has_session && can_*`; `OptimisticPlayPauseTarget()` keluar kosong tanpa target actionable. |
| Host tidak mengirim fallback media key tanpa target valid. | Terbukti via source/verifier | Fallback host hanya jalan saat ada session valid atau Media Player UIA menemukan now-playing yang actionable. |
| Paket runtime tetap kecil. | Terbukti via build/package | `out\dist\WidgetMusic` dibuat ulang dari Release dan berukuran sekitar 250,7 KB, tanpa PDB/intermediate. |
| Toggle compact/full bekerja di runtime Explorer. | Terbukti via inspeksi runtime | Inspeksi taskbar menunjukkan `WidgetMusicDeskbandWindow` berubah `132x40 -> 300x40 -> 132x40` saat command mode dikirim melalui jalur menu (`WM_COMMAND` id compact/full). |
| CPU tetap ringan setelah toggle. | Terbukti via runtime sample | Sampling 8 detik setelah kembali compact: `explorer.exe` sekitar 0,0312 detik CPU delta dan `WidgetMusicHost.exe` 0 detik CPU delta. |

## Perintah Audit

```powershell
.\scripts\Verify-WidgetMusicGoal.ps1 Release
```

Perintah ini mengecek invariant build/source utama: package size, compact/full mode, title reveal compact, guard tombol media, startup paint, marquee, icon sizing, dan lifecycle host.
