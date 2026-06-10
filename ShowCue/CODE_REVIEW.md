# Show Control - Code Review Report

**Project**: Show Control (Audio Cue Management Application)  
**Framework**: JUCE (C++)  
**Language**: English with Vietnamese UI strings  
**Date**: 2026-05-30

---

## Executive Summary

Show Control is a JUCE-based GUI application for managing audio playback in theatre/show environments. The architecture is well-organized with modular components, but there are several code quality issues ranging from bugs to memory management concerns that should be addressed.

**Overall Assessment**: **Moderate Quality** - Functional but requires maintenance

---

## Architecture Overview

### Components

| Component | Purpose | Status |
|-----------|---------|--------|
| **MainComponent** | Central orchestrator, layout management, project persistence | Core ✓ |
| **SoundPad** | Individual audio playback unit with UI, supports grid/list rendering | Core ✓ |
| **MasterDeckPanel** | Master volume control, playhead, transport controls | Core ✓ |
| **SidebarPanel** | List management, search functionality, theme control | Core ✓ |
| **InspectorPanel** | Detailed audio editing (waveform trimming, metadata) | Partial* |
| **AudioAnalyzer** | RMS calculation, volume normalization | Utility ✓ |
| **ErrorHandler** | Logging and error dialogs | Utility ✓ |
| **CueListPanel** | QLab-style cue list (header only, not integrated) | Stub ⚠ |
| **HotkeyManager** | Keyboard/MIDI binding system (header only) | Stub ⚠ |
| **AudioMetadataReader** | ID3/metadata reading utility (header only) | Stub ⚠ |

*InspectorPanel.h is truncated in the codebase at line 100

---

## Critical Issues

### 1. **BUG: Vietnamese Character Normalization - MainComponent.cpp:79**

**Location**: `MainComponent.cpp:69-93` - `cleanVietnameseString()`

**Severity**: High - String search will fail for Vietnamese 'u' characters

```cpp
const wchar_t* u = L"ùúủũụưừứửữựúùủũụưứừửữự";
for (int i = 0; i != 0; ++i) s = s.replaceCharacter (u[i], 'u');  // ← BUG!
```

**Issue**: The loop condition `i != 0` is never true after initialization. Should be `u[i] != 0`.

**Impact**: Searching for Vietnamese songs with 'ù/ú/ủ/ũ/ụ/ư' characters won't work properly.

**Fix**:
```cpp
for (int i = 0; u[i] != 0; ++i) s = s.replaceCharacter (u[i], 'u');
```

---

### 2. **Thread Safety Issue - SoundPad.h:10-16 (VolumeNormalizer)**

**Severity**: High - Potential undefined behavior

```cpp
void analyzeAudioFile (const juce::File& file, juce::AudioFormatManager& formatManager) {
    isAnalyzing = true;
    juce::Thread::launch ([this, file, &formatManager]() {  // ← PROBLEM: Reference to formatManager
        analyzedRMS = AudioAnalyzer::calculateFileRMS (file, formatManager);
        triggerAsyncUpdate();
    });
}
```

**Issue**: Lambda captures `formatManager` by reference but runs asynchronously. If the caller's `formatManager` is destroyed before the thread completes, undefined behavior occurs.

**Impact**: Audio analysis may crash in edge cases.

**Fix**: Capture by value or use a thread-safe reference:
```cpp
juce::Thread::launch ([this, file, fm = std::make_shared<juce::AudioFormatManager>(formatManager)]() {
    analyzedRMS = AudioAnalyzer::calculateFileRMS (file, *fm);
    triggerAsyncUpdate();
});
```

---

### 3. **Memory Management - Manual new/delete**

**Severity**: Medium - Potential memory leaks under exception conditions

**Locations**:
- `MainComponent.cpp:282, 324` - `new SoundPad()` allocated directly
- `MainComponent.cpp:273` - `std::make_unique<>` used (good)
- `SidebarPanel.h:180` - `new juce::AlertWindow()` allocated directly

**Issue**: JUCE uses `new` liberally, but mixing with no consistent ownership model is risky.

**Recommendation**: Use `std::make_unique` consistently:
```cpp
// Instead of:
auto* p = new SoundPad();

// Use:
auto p = std::make_unique<SoundPad>();
current->pads.add(p.release());
```

---

## Moderate Issues

### 4. **Incomplete Component Integration**

**Severity**: Medium

The following components are defined but not fully integrated:
- **CueListPanel.h** - Stub header, no implementation
- **HotkeyManager.h** - Header-only utility, not used in main code
- **AudioMetadataReader.h** - Header-only utility, not used
- **InspectorPanel.h** - Truncated at ~100 lines (incomplete implementation)

**Impact**: Features are partially designed but not connected.

**Recommendation**: Either implement or remove these headers.

---

### 5. **Project File Storage Security**

**Location**: `MainComponent.cpp:446-508` (Project save/load)

**Severity**: Low-Medium

```cpp
juce::File projectFile = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
    .getChildFile("Show_Control_Project.dat");
```

**Issue**: Project files stored in plaintext XML in home directory. No encryption or validation.

**Impact**: User projects readable/modifiable by other users on shared systems.

**Recommendation**: 
- Validate XML structure before loading
- Consider encryption for sensitive metadata
- Add version validation

---

### 6. **Resource Cleanup in Destructor**

**Location**: `MainComponent.cpp:57-67` (Destructor)

**Severity**: Low - Order of operations issue

```cpp
MainComponent::~MainComponent() {
    saveProject();              // Good
    stopTimer();                // Good
    viewScroller.setViewedComponent(nullptr);  // Good - prevents double-delete
    scrollContent.reset();      // Good - unique_ptr cleanup
    deviceManager.removeAudioCallback(&sourcePlayer);  // Good
    sourcePlayer.setSource(nullptr);  // Critical!
    mixerSource.removeAllInputs();    // Good
    allLists.clear();          // OwnedArray cleanup - good
}
```

**Current State**: Actually well-handled. The destructor properly shuts down audio before cleanup.

---

## Code Quality Issues

### 7. **Long Functions with Complex Logic**

**Locations**:
- `MainComponent.cpp:269-289` - `triggerManualMusicIngestion()` (moderate)
- `MainComponent.cpp:295-342` - `filesDropped()` (complex)
- `SoundPad.h:187-247` - `paintGridMode()` (very long paint function)

**Issue**: Functions exceeding 50 lines with nested conditionals. Hard to test and maintain.

**Recommendation**: Extract smaller functions:
```cpp
// Instead of huge paintGridMode(), extract:
void paintWaveform(juce::Graphics&, const juce::Rectangle<float>&);
void paintPlayProgress(juce::Graphics&, const juce::Rectangle<float>&);
void paintMetadata(juce::Graphics&, const juce::Rectangle<float>&);
```

---

### 8. **No const-correctness in Some Places**

**Location**: `MasterDeckPanel.h:156-173` (timerCallback)

```cpp
void timerCallback() override {
    if (activePad != nullptr && activePad->isPlaying()) {
        auto& transport = activePad->getTransportSource();  // Non-const reference
        // ...
    }
}
```

**Issue**: Some getters return non-const references when they shouldn't.

**Recommendation**: Audit all getter methods for proper const qualification.

---

### 9. **Magic Numbers**

**Severity**: Low

Hardcoded values throughout:
- `MainComponent.cpp:192` - `gridSizeSlider.setRange(140.0, 300.0, 1.0)`
- `SoundPad.h:394` - `int padHeight = static_cast<int>(w * 0.77f);`
- `MasterDeckPanel.h:240-243` - Position calculations with hardcoded values

**Recommendation**: Define constants:
```cpp
static constexpr float GRID_PAD_ASPECT_RATIO = 0.77f;
static constexpr int MIN_GRID_SIZE = 140;
static constexpr int MAX_GRID_SIZE = 300;
```

---

## Style & Maintainability

### 10. **Naming Consistency**

**Issues**:
- Mix of camelCase and UPPER_CASE for constants
- Abbreviated names in some places (`p`, `m`, `i`) inconsistently used
- Single-letter loop variables acceptable for small loops

**Recommendation**: Adopt consistent naming:
```cpp
// Current: mix of styles
for (auto* p : current->pads) { /* p is okay for tight loop */ }
int w = static_cast<int>(gridSizeSlider.getValue());  // w → padWidth

// Better:
for (auto* soundPad : currentList->pads) { /* clearer */ }
int padWidth = static_cast<int>(gridSizeSlider.getValue());
```

---

### 11. **Comment Quality**

**Issues**:
- Vietnamese comments mixed with English code
- Some comments are transliteration of Vietnamese
- Comments sometimes state WHAT instead of WHY

**Examples**:
```cpp
// Current (translated Vietnamese):
// --- FIXED: Giải nén bounds thành x, y, w, h để khớp chuẩn hàm 10 tham số của lớp juce::Path ---
p.addRoundedRectangle(...);

// Better:
// Unpack bounds for juce::Path::addRoundedRectangle which requires 10 parameters
p.addRoundedRectangle(...);
```

**Recommendation**: 
- Use English for code comments (translators can read code)
- Explain WHY, not WHAT
- Keep comments synchronized with code

---

## Testing Observations

### 12. **No Unit Tests Found**

**Severity**: Medium

The project appears to have no automated tests.

**Impact**: 
- Refactoring is risky
- Regression detection is manual
- Critical paths (audio playback, file I/O) untested

**Recommendation**:
```
Create test suite for:
├── AudioAnalyzer::calculateFileRMS()
├── cleanVietnameseString() function
├── Project save/load cycle
└── SoundPad state transitions
```

---

## Performance Considerations

### 13. **Audio Callback Blocking**

**Location**: `MainComponent.h:43-50` (MasterMixerSource)

**Current Implementation**:
```cpp
void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
    juce::MixerAudioSource::getNextAudioBlock(info);
    info.buffer->applyGain(...);  // ← In audio thread!
}
```

**Status**: ✓ Safe - `applyGain()` is realtime-safe

**Observation**: Good practice of keeping audio callback minimal.

---

### 14. **UI Repaint Frequency**

**Location**: Multiple `timerCallback()` implementations

```cpp
// SoundPad.h:162-181
void timerCallback() override {
    // ... calculations ...
    repaint();  // Called every 40ms
}

// MasterDeckPanel.h:156-173
void timerCallback() override {
    // ... calculations ...
    repaint();  // Called every 40ms
}
```

**Observation**: Two components repainting at 40ms interval = potential 50 FPS UI refresh.

**Assessment**: Acceptable for UI-heavy application, but could be optimized with single timer.

---

## Recommendations Priority

### 🔴 CRITICAL (Fix immediately)

1. Fix Vietnamese 'u' character normalization bug (line 79)
2. Fix thread safety issue in VolumeNormalizer (line 10-16)

### 🟠 HIGH (Fix before release)

3. Complete or remove stub components (CueListPanel, etc.)
4. Add unit tests for critical paths
5. Implement const-correctness audit

### 🟡 MEDIUM (Plan for next iteration)

6. Refactor long paint functions
7. Extract magic numbers to constants
8. Standardize memory management (unique_ptr)
9. Complete InspectorPanel implementation
10. Add error handling for file operations

### 🟢 LOW (Nice to have)

11. Standardize commenting style
12. Add performance monitoring
13. Create automated build tests

---

## File-by-File Summary

| File | Lines | Status | Notes |
|------|-------|--------|-------|
| MainComponent.h | 64 | ✓ Good | Clear interface |
| MainComponent.cpp | 509 | ⚠ Needs Work | Bug at line 79, complex functions |
| SoundPad.h | 300 | ⚠ Needs Work | Long paint functions, thread safety issue |
| MasterDeckPanel.h | 251 | ✓ Good | Well-structured, good error handling |
| SidebarPanel.h | 181 | ✓ Good | Clear UI logic |
| InspectorPanel.h | 100+ (truncated) | ? Unknown | File appears incomplete |
| AudioAnalyzer.h | 64 | ✓ Good | Simple, focused utility |
| ErrorHandler.h | 49 | ✓ Good | Minimal, focused |
| CueListPanel.h | 100+ | ⚠ Incomplete | Header only, stub implementation |
| HotkeyManager.h | 141 | ⚠ Incomplete | Header only, not integrated |
| AudioMetadataReader.h | 152 | ⚠ Incomplete | Header only, not used |

---

## Conclusion

Show Control demonstrates solid JUCE framework usage with good component architecture and UI design. However, the codebase has several maintainability issues and at least two bugs that should be fixed before production use:

1. **String normalization bug** will break Vietnamese character search
2. **Thread safety issue** in audio analysis could cause crashes

The stub components (CueListPanel, HotkeyManager, AudioMetadataReader) suggest planned features that weren't completed. Consider either finishing the implementation or removing these headers to reduce confusion.

**Estimated effort to address critical issues**: 4-6 hours  
**Recommended next steps**: 
1. Fix critical bugs immediately
2. Add unit tests
3. Complete or remove stub components
4. Perform code review of audio pipeline for real-time safety
