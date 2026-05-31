# Catatan Perbaikan Widget Music

Dokumen ini mencatat perbaikan penting yang sudah dilakukan pada project Widget Music agar riwayat teknisnya mudah dilacak.

## Tujuan V1

- Deskband native Windows 10 tetap ringan karena berjalan di Explorer.
- Host terpisah membaca dan mengontrol media session Windows dengan aman.
- Widget menampilkan title/artist/fallback yang rapi, tombol previous/play-pause/next, dan tetap stabil saat host atau media session berubah.

## Perbaikan Deskband

- Menyembunyikan title bawaan Explorer pada surface toolbar supaya yang tampil adalah UI custom, bukan teks "Widget Music" saja.
- Memperbaiki paint order: background digambar dulu, lalu teks, lalu tombol.
- Menjaga ukuran normal widget di sekitar 280-300px x 40px agar title masih punya ruang.
- Menambahkan deteksi area taskbar yang benar-benar terlihat. Jika task-list menimpa area kiri yang kosong, widget dipromosikan ke atas agar title tidak tersembunyi; jika taskbar penuh, widget tetap compact.
- Menghapus border/status overlay yang mengganggu area title.
- Menambahkan double-buffer paint untuk anti-flicker.
- Mengganti tombol ke rendering GDI+ anti-aliased agar icon lebih tajam.
- Menambahkan tooltip untuk tombol previous, play/pause, dan next.
- Membuat hover/pressed repaint hanya pada area tombol, bukan seluruh widget.
- Membuat back buffer dipakai ulang agar paint/marquee tidak membuat bitmap baru tiap frame.
- Meng-cache warna background taskbar sampai theme/ukuran berubah agar marquee tidak tersendat oleh sampling warna berkala.
- Menahan log deskband default; log detail hanya aktif saat debug registry/env diaktifkan.
- Mengganti auto-hide/collapse menjadi mode eksplisit: compact 132x40 dan full 300x40, dipilih lewat context menu klik kanan pada widget.
- Mode compact tetap menampilkan tombol media ketika ada target valid, tetapi tidak mencoba membuka/menutup otomatis berdasarkan idle atau hover.
- Saat mode compact dan lagu berganti, title tampil sementara sebagai popup native di atas widget agar tetap terlihat jelas tanpa mengubah layout tombol media.
- Perubahan ukuran compact/full memakai notifikasi resmi `DBID_BANDINFOCHANGED` dan resize sekali, supaya Explorer tetap stabil.
- Padding kanan tombol diperbesar agar tombol next tidak terlalu dekat dengan area widget taskbar lain seperti Weather.
- Resize internal deskband sekarang menjaga sisi kanan sebagai anchor saat pindah compact/full, supaya posisi kanan widget lebih stabil di area taskbar dekat Weather/tray.
- Paint awal/erase background mengisi warna taskbar segera, sehingga area deskband tidak flash hitam saat Explorer baru memuat widget.
- Icon play/pause diringankan: hit area tetap nyaman, tetapi ring visual diperkecil ke 28px dengan stroke lebih tipis; previous/next memakai glyph vector lebih compact.
- Trigger compact/full tidak lagi memakai icon tambahan di surface widget; perpindahan mode dilakukan lewat menu klik kanan.
- Marquee hanya aktif di mode full saat title panjang dan media sedang playing; compact title reveal memakai popup native terpisah agar layout compact tetap bersih.
- State awal deskband dibuat compact agar Explorer startup ringan; host baru mulai setelah delay sekitar 7 detik atau saat user membuka widget/menekan command.

## Perbaikan Title/Marquee

- Title panjang berjalan hanya saat media sedang playing.
- Marquee title dibuat speed-based sekitar 40px/detik dengan frame delay yang dibatasi dan jeda pendek di awal/loop, sehingga tidak terasa terlalu cepat atau meloncat saat Explorer sibuk.
- Scheduler marquee memakai timer queue yang mengirim `WM_APP_MARQUEE` ter-coalesce, sehingga frame tidak menumpuk dan meloncat saat Explorer sibuk.
- Marquee berhenti saat teks tidak overflow, widget tidak punya area teks, media pause/stop, atau window ditutup.
- Repaint marquee dibuat text-only: frame timer hanya membersihkan/menggambar area title, bukan tombol dan background penuh.
- Marquee memakai pre-rendered text strip: teks dirender sekali ke bitmap kecil, lalu frame animasi hanya menggeser potongan bitmap. Ini menjaga bentuk huruf/background konsisten antar-frame.
- Font title dipakai ulang; fallback render tetap native GDI/ClearType di posisi integer, sementara alpha pass dan blit akhir tetap dibatasi ke area yang berubah.
- State identik dari host diabaikan di host dan deskband, sehingga polling/event media tidak memicu full repaint yang bisa mengganggu marquee.
- State yang berubah secara internal tetapi tampilan teks/tombol tetap sama tetap disimpan, tetapi tidak memicu `WM_APP_STATE`/full repaint.
- `WM_WINDOWPOSCHANGED` tidak lagi memicu full repaint/background sample untuk perubahan z-order saja; repaint penuh hanya saat posisi/ukuran/visibility berubah.
- Refresh sample background dari resize/posisi ditunda selama marquee aktif, supaya background tidak berubah di tengah animasi teks.
- Frame marquee meminta paint langsung (`RedrawWindow` text-only) pada area title saja, sehingga animasi tidak menunggu paint queue Explorer yang bisa coalesce terlalu lama.

## Perbaikan Host

- Host tetap terpisah dari Explorer untuk membaca GlobalSystemMediaTransportControlsSessionManager.
- Command media diproses lewat worker thread agar klik widget tidak membekukan deskband.
- Play/pause memakai optimistic UI hanya saat ada media target yang actionable; kondisi kosong tidak boleh mengubah UI menjadi playing.
- Next/previous menandai pending track change agar metadata cepat diperbarui.
- Fallback command via media key hanya tersedia jika ada session/target media valid; host menolak fallback saat tidak ada aplikasi/media yang actionable.
- Metadata fallback dibuat ramah: Music, Spotify, Chrome, Edge, atau Now playing; raw package id tidak ditampilkan.
- Music/Groove/ZuneMusic diberi prioritas lebih tinggi dibanding browser session seperti Chrome/Edge saat beberapa session aktif.
- Host memakai event CurrentSessionChanged, SessionsChanged, PlaybackInfoChanged, dan MediaPropertiesChanged dengan polling ringan sebagai fallback.
- Startup host dibuat lebih stabil dengan retry/backoff saat GSMTC belum siap setelah Explorer restart.
- Host sekarang keluar saat koneksi pipe deskband diputus, sehingga menonaktifkan toolbar ikut menghentikan proses pendamping.
- Host juga keluar jika tidak ada deskband yang connect ke pipe dalam sekitar 8 detik, sehingga proses pendamping tidak tinggal hidup saat toolbar dimatikan sebelum koneksi selesai.
- Log update failure ditahan agar tidak spam saat Windows media manager belum siap.
- State host sekarang membedakan `has_session` agar idle menampilkan `No media`, bukan `Now playing`.
- Windows Media Player legacy (`wmplayer`) dikenali sebagai sumber musik dan diberi fallback nama yang ramah.
- Jalur show deskband tidak lagi memaksa `UpdateWindow` sinkron; audit overlap taskbar ditunda via timer singkat agar taskbar tidak terasa terkunci saat widget dimunculkan ulang.
- Tombol media tetap diaktifkan untuk session Music/Media Player yang valid walau capability GSMTC kurang lengkap; host akan mencoba GSMTC dulu lalu fallback ke media key.
- Jika GSMTC tidak memberi session tetapi Media Player sedang menampilkan now-playing di UI, host memakai fallback UIA terbatas untuk title dan tetap mengaktifkan kontrol media-key.
- Jika window Media Player ada tetapi now-playing belum tersedia, widget boleh menampilkan `Media Player`, tetapi tombol media tetap disabled sampai target now-playing valid.

## Script dan Diagnostik

- Build CLI tersedia lewat `scripts/Build.cmd`.
- Register/unregister tersedia lewat `scripts/Register-WidgetMusic.cmd` dan `scripts/Unregister-WidgetMusic.cmd`.
- Paket runtime bersih tersedia lewat `scripts/Package-WidgetMusic.cmd`; output `out/dist/WidgetMusic` tidak membawa PDB atau intermediate build artefact.
- Diagnostik taskbar tersedia lewat `scripts/Inspect-WidgetMusicTaskbar.ps1` untuk melihat posisi window deskband dan capture taskbar.
- Script diagnostik taskbar sekarang punya fallback enumerasi window global dan tidak gagal keras jika sesi automation tidak bisa mengakses `Shell_TrayWnd` atau screenshot desktop.
- Audit invariant goal tersedia lewat `scripts/Verify-WidgetMusicGoal.ps1 Release` untuk mengecek output build, compact/full mode, title reveal compact, guard tombol media, package size, dan lifecycle host.
- Pembaca log UTF-16 tersedia lewat `scripts/Read-WidgetMusicLogs.ps1 -Tail 80` agar log deskband/host dari `%TEMP%` bisa dibaca bersih.
- Audit requirement aktif terdokumentasi di `docs/Goal-Completion-Audit.md`, termasuk gap verifikasi visual yang masih membutuhkan inspeksi desktop langsung.

## Catatan Stabilitas

- Kode deskband harus tetap sangat ringan karena hidup di Explorer.
- Jangan memasukkan pembacaan media session berat ke deskband; semua akses GSMTC harus tetap di host.
- Jangan memakai overlay/injection/taskbar hack. Tetap gunakan deskband resmi.
- Fitur visual baru sebaiknya diukur dulu dampaknya pada repaint dan CPU.
- Jika title masih terasa berat, prioritas berikutnya adalah menurunkan kecepatan marquee atau menambahkan pause di ujung teks, bukan menambah beban render.

## Pengukuran Terakhir

- Build Release x64 setelah guard media, marquee speed-based, startup paint, dan icon sizing berhasil tanpa warning/error.
- Paket runtime bersih `out/dist/WidgetMusic` berukuran sekitar 244,7 KB; folder build 33 MB berasal dari PDB dan intermediate file, bukan runtime widget.
- Verifier terbaru berhasil mengecek package size, no-PDB dist, no-fake media control, icon size constants, marquee speed-based timing, startup background paint, dan guard command host.
- Build Release x64 setelah penggantian collapse menjadi compact/full berhasil tanpa warning/error; paket runtime bersih `out/dist/WidgetMusic` berukuran sekitar 250,7 KB.
- Runtime terbaru memuat `WidgetMusicDeskband.dll` di Explorer dan mulai dari compact `132x40`.
- Trigger mode sekarang ada di menu klik kanan (`Compact view` / `Full view`) agar surface compact tetap rapi tanpa icon tambahan.
- Sampling CPU 8 detik setelah kembali compact: `explorer.exe` sekitar 0,0312 detik CPU delta dan `WidgetMusicHost.exe` 0 detik CPU delta.
- Verifikasi runtime terbaru memakai command path menu (`WM_COMMAND`) menunjukkan ukuran widget berubah `132x40 -> 300x40 -> 132x40`.
- Sampling CPU 8 detik setelah patch opsi A + opsi 1: `explorer.exe` sekitar 0,0156 detik CPU delta dan `WidgetMusicHost.exe` sekitar 0,0156 detik CPU delta.
