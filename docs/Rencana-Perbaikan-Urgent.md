# Rencana Perbaikan Urgent - Widget Music

**Tanggal:** 31 Mei 2026  
**Status:** 🔴 URGENT - Masalah Performa & Visual

---

## 🚨 Masalah yang Teridentifikasi

### 1. Marquee Title Patah-Patah (CRITICAL)

**Gejala:**
- Title yang berjalan (marquee) terlihat patah-patah/stuttering
- Animasi tidak smooth seperti sebelumnya
- Terjadi setelah update terakhir

**Root Cause Analysis:**

#### Masalah di [`OnMarqueeTimer()`](WidgetMusicDeskband/src/Deskband.cpp:2131)
```cpp
// Line 2148-2153: Perhitungan advance yang bermasalah
if (elapsed > kMarqueeMaxFrameMs) elapsed = kMarqueeMaxFrameMs;
_marqueeOffsetSubPx += static_cast<int>(kMarqueeSpeedPxPerSec * elapsed * 256);
int advance = _marqueeOffsetSubPx / 1000;  // ❌ MASALAH: Pembagi 1000 tidak konsisten
_marqueeOffsetSubPx %= 1000;
```

**Analisis:**
1. **Inconsistent scaling**: Mengalikan dengan 256 tapi membagi dengan 1000
2. **Precision loss**: Sub-pixel calculation tidak akurat
3. **Frame skipping**: Saat Explorer sibuk, elapsed bisa > 48ms, menyebabkan jump

#### Masalah di [`RedrawWindow()`](WidgetMusicDeskband/src/Deskband.cpp:2162)
```cpp
// Line 2162: Repaint yang terlalu agresif
::RedrawWindow(_hwnd, &_textRc, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE | RDW_NOCHILDREN);
```

**Analisis:**
- `RDW_UPDATENOW` memaksa paint synchronous
- Bisa menyebabkan blocking saat Explorer sibuk
- Tidak ada throttling untuk frame rate

---

### 2. Background Tidak Native Windows 10 (HIGH)

**Gejala dari Screenshot:**
- Background widget terlihat berbeda dari taskbar
- Warna tidak match dengan taskbar Windows 10
- Terlihat seperti "kotak" yang menempel, bukan native

**Root Cause Analysis:**

#### Masalah di [`SampleAdjacentTaskbarColor()`](WidgetMusicDeskband/src/Deskband.cpp:317)
```cpp
// Line 325-331: Sampling points yang tidak optimal
POINT points[] = {
    {wr.left - 6, y},   // Terlalu dekat dengan widget
    {wr.left - 18, y},
    {wr.left - 32, y},
    {wr.right + 6, y},  // Bisa sample icon/button lain
    {wr.right + 18, y},
};
```

**Analisis:**
1. **Sampling location**: Points terlalu dekat dengan widget, bisa sample area yang sudah ter-overlay
2. **No vertical sampling**: Hanya sample horizontal, tidak consider taskbar gradient
3. **Limited samples**: Hanya 5 points, tidak cukup untuk average yang akurat

#### Masalah di Paint Logic
```cpp
// Line 2213-2217: Cache yang tidak di-invalidate dengan benar
if (_cachedBgValid) {
    bgSample = _cachedBg;
} else {
    bgSample = SampleAdjacentTaskbarColor(_hwnd, ::GetSysColor(COLOR_3DFACE));
    _cachedBg = bgSample;
    _cachedBgValid = true;
}
```

**Analisis:**
- Cache tidak di-refresh saat taskbar theme berubah
- Tidak detect perubahan accent color Windows
- Tidak handle taskbar transparency/blur

---

## 🔧 Solusi Detail

### Perbaikan 1: Fix Marquee Stuttering

#### A. Perbaiki Sub-Pixel Calculation
```cpp
// BEFORE (Line 2148-2153)
if (elapsed > kMarqueeMaxFrameMs) elapsed = kMarqueeMaxFrameMs;
_marqueeOffsetSubPx += static_cast<int>(kMarqueeSpeedPxPerSec * elapsed * 256);
int advance = _marqueeOffsetSubPx / 1000;
_marqueeOffsetSubPx %= 1000;

// AFTER - Konsisten dengan fixed-point arithmetic
if (elapsed > kMarqueeMaxFrameMs) elapsed = kMarqueeMaxFrameMs;

// Gunakan 256 sebagai fixed-point scale (8-bit fractional)
// Formula: pixels = (speed_px_per_sec * elapsed_ms * 256) / 1000
_marqueeOffsetSubPx += (kMarqueeSpeedPxPerSec * elapsed * 256) / 1000;

// Extract integer pixels (shift right 8 bits)
int advance = _marqueeOffsetSubPx >> 8;
_marqueeOffsetSubPx &= 0xFF; // Keep only fractional part

if (advance <= 0) return;
_marqueeOffsetPx += advance;
```

**Penjelasan:**
- Menggunakan fixed-point arithmetic yang konsisten (8-bit fractional)
- Shift operation lebih cepat dari division
- Fractional part tetap akurat dengan bitwise AND

#### B. Smooth Repaint dengan Frame Limiting
```cpp
// BEFORE (Line 2162)
::RedrawWindow(_hwnd, &_textRc, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE | RDW_NOCHILDREN);

// AFTER - Async repaint dengan throttling
if (_hwnd) {
    // Hanya invalidate, biarkan Windows schedule paint
    ::InvalidateRect(_hwnd, &_textRc, FALSE);
    
    // Optional: Force update jika frame rate terlalu rendah
    DWORD now = ::GetTickCount();
    if (now - _lastMarqueeRepaintTick > 32) { // Max 30fps
        ::UpdateWindow(_hwnd);
        _lastMarqueeRepaintTick = now;
    }
}
```

**Penjelasan:**
- Tidak memaksa synchronous paint
- Throttle ke max 30fps untuk smooth animation
- Biarkan Windows coalesce multiple invalidates

#### C. Tambahkan Frame Time Smoothing
```cpp
// Tambahkan member variables
DWORD _marqueeFrameHistory[4] = {16, 16, 16, 16};
int _marqueeFrameHistoryIndex = 0;

// Di OnMarqueeTimer(), setelah calculate elapsed
DWORD smoothedElapsed = elapsed;
if (elapsed > 0 && elapsed < 100) {
    // Store in history
    _marqueeFrameHistory[_marqueeFrameHistoryIndex] = elapsed;
    _marqueeFrameHistoryIndex = (_marqueeFrameHistoryIndex + 1) % 4;
    
    // Calculate average of last 4 frames
    DWORD sum = 0;
    for (int i = 0; i < 4; i++) {
        sum += _marqueeFrameHistory[i];
    }
    smoothedElapsed = sum / 4;
}

// Use smoothedElapsed instead of elapsed for calculation
```

**Penjelasan:**
- Smooth out frame time spikes
- Prevent sudden jumps saat Explorer sibuk
- Moving average dari 4 frames terakhir

---

### Perbaikan 2: Fix Background Native Look

#### A. Improved Taskbar Color Sampling
```cpp
// REPLACE SampleAdjacentTaskbarColor() function
COLORREF SampleAdjacentTaskbarColor(HWND hwnd, COLORREF fallback) {
    RECT wr{};
    if (!hwnd || !::GetWindowRect(hwnd, &wr)) return fallback;
    
    HDC screen = ::GetDC(nullptr);
    if (!screen) return fallback;
    
    // Sample dari area yang lebih jauh dan lebih banyak points
    const int y = (wr.top + wr.bottom) / 2;
    const int yTop = wr.top + 4;
    const int yBottom = wr.bottom - 4;
    
    POINT points[] = {
        // Horizontal samples (lebih jauh dari widget)
        {wr.left - 40, y},
        {wr.left - 60, y},
        {wr.left - 80, y},
        {wr.right + 40, y},
        {wr.right + 60, y},
        {wr.right + 80, y},
        
        // Vertical samples (untuk detect gradient)
        {wr.left - 50, yTop},
        {wr.left - 50, yBottom},
        {wr.right + 50, yTop},
        {wr.right + 50, yBottom},
    };
    
    int sumR = 0, sumG = 0, sumB = 0;
    int count = 0;
    
    for (const auto& pt : points) {
        COLORREF c = ::GetPixel(screen, pt.x, pt.y);
        if (!IsReasonableThemeSample(c)) continue;
        sumR += GetRValue(c);
        sumG += GetGValue(c);
        sumB += GetBValue(c);
        ++count;
    }
    
    ::ReleaseDC(nullptr, screen);
    
    if (count == 0) return fallback;
    
    // Average color
    COLORREF avgColor = RGB(sumR / count, sumG / count, sumB / count);
    
    // Slight adjustment untuk match taskbar better
    // Taskbar biasanya sedikit lebih gelap dari sample
    int r = GetRValue(avgColor);
    int g = GetGValue(avgColor);
    int b = GetBValue(avgColor);
    
    // Darken by 3% untuk match taskbar depth
    r = (r * 97) / 100;
    g = (g * 97) / 100;
    b = (b * 97) / 100;
    
    return RGB(r, g, b);
}
```

**Penjelasan:**
- Sample dari 10 points (vs 5 sebelumnya)
- Sample lebih jauh dari widget (40-80px vs 6-32px)
- Include vertical samples untuk detect gradient
- Slight darkening untuk match taskbar depth

#### B. Better Cache Invalidation
```cpp
// Tambahkan member variable
COLORREF _lastAccentColor = CLR_INVALID;

// Di RequestBackgroundRefresh()
void RequestBackgroundRefresh(bool force) {
    // Check if accent color changed
    COLORREF currentAccent = ::GetSysColor(COLOR_HIGHLIGHT);
    if (_lastAccentColor != currentAccent) {
        force = true;
        _lastAccentColor = currentAccent;
    }
    
    if (force || !_marqueeActive) {
        _cachedBgValid = false;
        _pendingBgRefresh = false;
        return;
    }
    
    _pendingBgRefresh = true;
}

// Tambahkan handler untuk WM_DWMCOLORIZATIONCOLORCHANGED
case WM_DWMCOLORIZATIONCOLORCHANGED:
    RequestBackgroundRefresh(true);
    ::InvalidateRect(hwnd, nullptr, FALSE);
    return 0;
```

**Penjelasan:**
- Detect accent color changes
- Handle DWM colorization changes
- Force refresh saat theme berubah

#### C. Use DWM API untuk Better Integration
```cpp
// Tambahkan di header
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// Function baru untuk get taskbar color via DWM
COLORREF GetTaskbarColorViaDWM() {
    BOOL enabled = FALSE;
    if (FAILED(::DwmIsCompositionEnabled(&enabled)) || !enabled) {
        return CLR_INVALID;
    }
    
    // Get DWM colorization color
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(::DwmGetColorizationColor(&color, &opaque))) {
        // Extract RGB from ARGB
        BYTE r = (color >> 16) & 0xFF;
        BYTE g = (color >> 8) & 0xFF;
        BYTE b = color & 0xFF;
        
        // Blend dengan taskbar base color
        COLORREF baseColor = ::GetSysColor(COLOR_3DFACE);
        
        // Jika opaque, gunakan langsung
        if (opaque) {
            return RGB(r, g, b);
        }
        
        // Jika transparent, blend dengan base
        return Blend(baseColor, RGB(r, g, b), 20);
    }
    
    return CLR_INVALID;
}

// Update ResolveImmediateBackground()
COLORREF ResolveImmediateBackground(HWND hwnd) {
    if (IsHighContrast()) return ::GetSysColor(COLOR_BTNFACE);
    
    if (!_cachedBgValid && hwnd) {
        // Try DWM first
        COLORREF dwmColor = GetTaskbarColorViaDWM();
        if (dwmColor != CLR_INVALID) {
            _cachedBg = dwmColor;
        } else {
            // Fallback to sampling
            _cachedBg = SampleAdjacentTaskbarColor(hwnd, ::GetSysColor(COLOR_3DFACE));
        }
        _cachedBgValid = true;
    }
    
    return _cachedBgValid ? _cachedBg : ::GetSysColor(COLOR_3DFACE);
}
```

**Penjelasan:**
- Gunakan DWM API untuk get official taskbar color
- Fallback ke sampling jika DWM tidak available
- Blend dengan base color untuk transparency

---

## 📋 Implementation Plan

### Phase 1: Fix Marquee (URGENT - 1 hari)

**Priority: 🔴 CRITICAL**

#### Step 1: Fix Sub-Pixel Math (2 jam)
- [ ] Update [`OnMarqueeTimer()`](WidgetMusicDeskband/src/Deskband.cpp:2148) dengan fixed-point arithmetic
- [ ] Test dengan berbagai speeds (20, 40, 60 px/sec)
- [ ] Verify smooth animation

#### Step 2: Smooth Repaint (2 jam)
- [ ] Replace `RedrawWindow()` dengan `InvalidateRect()` + throttling
- [ ] Add frame time tracking
- [ ] Test dengan Explorer busy (buka banyak windows)

#### Step 3: Frame Smoothing (2 jam)
- [ ] Add frame history buffer
- [ ] Implement moving average
- [ ] Test dengan CPU load tinggi

#### Step 4: Testing & Verification (2 jam)
- [ ] Test marquee dengan title panjang
- [ ] Test dengan berbagai media players
- [ ] Verify CPU usage tetap rendah
- [ ] Check tidak ada memory leak

**Total Effort: 8 jam (1 hari kerja)**

---

### Phase 2: Fix Background (HIGH - 1 hari)

**Priority: 🟡 HIGH**

#### Step 1: Improved Sampling (3 jam)
- [ ] Update [`SampleAdjacentTaskbarColor()`](WidgetMusicDeskband/src/Deskband.cpp:317)
- [ ] Add more sample points (10 vs 5)
- [ ] Add vertical sampling
- [ ] Add darkening adjustment
- [ ] Test dengan berbagai taskbar positions

#### Step 2: DWM Integration (3 jam)
- [ ] Add `GetTaskbarColorViaDWM()` function
- [ ] Update `ResolveImmediateBackground()`
- [ ] Handle DWM composition changes
- [ ] Test dengan DWM enabled/disabled

#### Step 3: Better Cache Invalidation (1 jam)
- [ ] Add accent color tracking
- [ ] Handle `WM_DWMCOLORIZATIONCOLORCHANGED`
- [ ] Test theme changes

#### Step 4: Testing & Verification (1 jam)
- [ ] Test dengan berbagai Windows themes
- [ ] Test dengan accent colors berbeda
- [ ] Test dengan taskbar transparency
- [ ] Verify match dengan native taskbar

**Total Effort: 8 jam (1 hari kerja)**

---

## 🧪 Testing Checklist

### Marquee Testing
- [ ] Title panjang (>300px) berjalan smooth
- [ ] Tidak ada stuttering saat Explorer sibuk
- [ ] Pause di awal dan loop bekerja
- [ ] CPU usage < 0.05s per 10s
- [ ] Tidak ada memory leak setelah 1 jam
- [ ] Frame rate consistent ~30fps

### Background Testing
- [ ] Background match dengan taskbar
- [ ] Tidak terlihat seperti "kotak"
- [ ] Smooth transition saat theme change
- [ ] Work dengan dark/light theme
- [ ] Work dengan custom accent colors
- [ ] Work dengan taskbar transparency

### Integration Testing
- [ ] Marquee + background bekerja bersamaan
- [ ] Tidak ada visual glitch
- [ ] Compact/full mode switch smooth
- [ ] Tidak ada flicker
- [ ] Performance tetap optimal

---

## 📊 Expected Results

### Before Fix
- ❌ Marquee patah-patah, stuttering
- ❌ Background tidak match taskbar
- ❌ Terlihat tidak native
- ⚠️ User experience buruk

### After Fix
- ✅ Marquee smooth 30fps
- ✅ Background perfect match dengan taskbar
- ✅ Terlihat native Windows 10
- ✅ User experience excellent

### Performance Impact
- CPU usage: Tetap < 0.05s per 10s (no regression)
- Memory: Tetap < 10MB (no regression)
- Paint time: < 16ms (60fps capable)
- Startup: Tetap < 500ms (no regression)

---

## 🔍 Root Cause Summary

### Marquee Issue
**Primary Cause:** Inconsistent fixed-point arithmetic
- Multiply by 256 but divide by 1000
- Precision loss in sub-pixel calculation
- Synchronous repaint blocking animation

**Secondary Cause:** No frame smoothing
- Frame time spikes tidak di-handle
- Sudden jumps saat Explorer sibuk

### Background Issue
**Primary Cause:** Poor sampling strategy
- Sample points terlalu dekat
- Tidak cukup samples untuk accurate average
- Tidak consider taskbar gradient

**Secondary Cause:** Cache invalidation
- Tidak detect theme changes properly
- Tidak use DWM API untuk official color

---

## 💡 Additional Improvements (Optional)

### 1. Adaptive Marquee Speed
```cpp
// Adjust speed based on text length
int adaptiveSpeed = kMarqueeSpeedPxPerSec;
if (_marqueeTextWidth > 500) {
    adaptiveSpeed = 50; // Faster untuk text panjang
} else if (_marqueeTextWidth < 200) {
    adaptiveSpeed = 30; // Slower untuk text pendek
}
```

### 2. Easing Function untuk Marquee
```cpp
// Smooth start/stop dengan easing
float EaseInOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
}

// Apply saat start/loop
if (_marqueeJustStarted) {
    float progress = (now - _marqueeStartTick) / 500.0f; // 500ms ease-in
    if (progress < 1.0f) {
        advance = static_cast<int>(advance * EaseInOutQuad(progress));
    }
}
```

### 3. Background Blur Effect (Windows 10 Acrylic)
```cpp
// Use Windows 10 Acrylic effect untuk modern look
#include <windows.ui.composition.interop.h>

// Apply acrylic blur
ACCENT_POLICY accent = { ACCENT_ENABLE_ACRYLICBLURBEHIND, 0, 0, 0 };
WINDOWCOMPOSITIONATTRIBDATA data = { WCA_ACCENT_POLICY, &accent, sizeof(accent) };
SetWindowCompositionAttribute(_hwnd, &data);
```

---

## ✅ Success Criteria

### Must Have
1. ✅ Marquee smooth tanpa stuttering
2. ✅ Background match 100% dengan taskbar
3. ✅ No performance regression
4. ✅ No visual glitches

### Should Have
1. ✅ Frame rate consistent 30fps
2. ✅ Work dengan semua Windows themes
3. ✅ Smooth theme transitions
4. ✅ Native Windows 10 look

### Nice to Have
1. ⚠️ Adaptive marquee speed
2. ⚠️ Easing animations
3. ⚠️ Acrylic blur effect

---

## 🚀 Next Steps

1. **Immediate (Today)**
   - Review rencana ini dengan team
   - Setup test environment
   - Backup current code (Git tag)

2. **Day 1 (Tomorrow)**
   - Implement marquee fixes
   - Test thoroughly
   - Commit changes

3. **Day 2**
   - Implement background fixes
   - Test thoroughly
   - Commit changes

4. **Day 3**
   - Integration testing
   - Performance verification
   - User acceptance testing

5. **Day 4**
   - Bug fixes jika ada
   - Documentation update
   - Release preparation

**Total Timeline: 2-4 hari untuk complete fix**

---

## 📝 Notes

- Semua changes harus di-test dengan `.\scripts\Verify-WidgetMusicGoal.ps1 Release`
- Commit setiap logical change (jangan commit semua sekaligus)
- Update [`docs/Catatan-Perbaikan.md`](docs/Catatan-Perbaikan.md:1) setelah selesai
- Screenshot before/after untuk documentation

**Prioritas: Fix marquee dulu (lebih critical), baru background**
