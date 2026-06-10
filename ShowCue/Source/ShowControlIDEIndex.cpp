// File chỉ để clangd index đủ header (header-only / chưa gắn UI).
// Không thêm logic runtime — linker gộp TU rỗng, không ảnh hưởng app.

#include <JuceHeader.h>

#include "AudioAnalyzer.h"
#include "AudioMetadataReader.h"
#include "CueListPanel.h"
#include "ErrorHandler.h"
#include "HotkeyManager.h"
#include "HotkeyAssignDialog.h"
#include "VideoAudioExtractor.h"
#include "FfmpegSetupDialog.h"
#include "AudioDeviceSettingsPanel.h"
#include "BusMixerPanel.h"
#include "InspectorPanel.h"
#include "PadEqualizerDialog.h"
#include "MainComponent.h"
#include "MasterDeckPanel.h"
#include "PadCueState.h"
#include "PadRealtimeSource.h"
#include "ShowDsp.h"
#include "ShowGraphicsSafe.h"
#include "ShowAudioFormats.h"
#include "ShowControlLookAndFeel.h"
#include "ShowTheme.h"
#include "SidebarPanel.h"
#include "SoundPad.h"
#include "StageMonitorComponent.h"

#if defined(__clang__)
 #pragma clang diagnostic ignored "-Wunused-function"
#endif

namespace showcontrol::ide_index
{
static void touchTypesForIDE()
{
    juce::ignoreUnused (sizeof (AudioAnalyzer));
    juce::ignoreUnused (sizeof (CueListPanel));
    juce::ignoreUnused (sizeof (HotkeyManager));
    juce::ignoreUnused (sizeof (MainComponent));
    juce::ignoreUnused (sizeof (PadCueState));
    juce::ignoreUnused (sizeof (PadRealtimeSource));
    juce::ignoreUnused (sizeof (PadDspChain));
    juce::ignoreUnused (sizeof (MasterOutputDynamics));
    juce::ignoreUnused (sizeof (PadParametricEq6));
    juce::ignoreUnused (sizeof (showcontrol::ui::Peq6CurveDisplay));
    juce::ignoreUnused (sizeof (showcontrol::gfx::sanitise (juce::Rectangle<int> {})));
    juce::ignoreUnused (sizeof (ShowTheme::Palette));
    juce::ignoreUnused (sizeof (ShowControlLookAndFeel));
}
} // namespace showcontrol::ide_index
