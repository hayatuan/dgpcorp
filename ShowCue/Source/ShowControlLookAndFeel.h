#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ShowTheme.h"
#include "ShowFlatIcons.h"
#include "MasterDeckComponent.h"

/**
 * Global LookAndFeel cho toàn bộ ứng dụng ShowCue.
 *
 * Real-time safe: class này chỉ chạy trên Message Thread (UI), không bao giờ được
 * gọi từ audio callback. Tất cả setColour() đều là UI-side, an toàn.
 *
 * Cách dùng:
 *   - Tạo một instance duy nhất trong MainComponent
 *   - Gọi juce::LookAndFeel::setDefaultLookAndFeel(&appLookAndFeel) trong constructor
 *   - Gọi setDarkMode(isDark) + juce::LookAndFeel::setDefaultLookAndFeel(&appLookAndFeel)
 *     mỗi khi theme thay đổi để broadcast xuống toàn bộ widget tree
 *   - Gọi juce::LookAndFeel::setDefaultLookAndFeel(nullptr) trong destructor
 */
class ShowControlLookAndFeel : public juce::LookAndFeel_V4
{
public:
    /** Colour IDs đăng ký qua applyPalette — component đọc bằng findColour, không hardcode hex. */
    enum ColourIds
    {
        appBackgroundColourId   = 0x2003000,
        panelBackgroundColourId,
        panelBorderColourId,
        textPrimaryColourId,
        textSecondaryColourId,
        accentColourId,
        successColourId,
        padBorderColourId,
        padPlayingBorderColourId,
        sliderTrackColourId,
        sliderThumbColourId
    };

    ShowControlLookAndFeel();
    ~ShowControlLookAndFeel() override;

    void setDarkMode (bool dark);
    bool isDarkMode() const noexcept { return currentIsDark; }

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& font) override;

    /** Xóa cache typeface và broadcast sau khi đổi theme / nạp font mới. */
    void refreshTypography();

    /** Gọi trước khi hủy LAF / thoát app — tránh leak Typeface + mutex invalid. */
    static void shutdownTypographyCaches() noexcept;

    /** Slider ngang/dọc: track 3px accent-tint, thumb tròn 10px. */
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override;

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getTabButtonFont (juce::TabBarButton&, float height) override;

    /** Font chuột phải toàn cục — Roboto 15pt nhúng nhị phân. */
    juce::Font getPopupMenuFont() override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;

    /** TextEditor phẳng bo góc — nền chìm, viền xanh thương hiệu khi focus. */
    void fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                   juce::TextEditor& textEditor) override;

    void drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                juce::TextEditor& textEditor) override;

    /** Cấu hình màu + padding cho ô sửa tên danh sách BGM/CUE trong Sidebar. */
    static void applyInlineListNameEditorStyle (juce::TextEditor& editor, bool isDark,
                                                bool isOnSelectedRow = false) noexcept;

    /** Nhãn tên track — double-click / showEditor(), đồng bộ LookAndFeel inline. */
    static void applyTrackNameLabelStyle (juce::Label& label, bool isDark,
                                          bool isOnSelectedRow = false) noexcept;

    //==============================================================================
    /** Scrollbar mỏng kiểu macOS — chỉ vẽ thumb, không vẽ track để blend với nền. */
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar& /*bar*/,
                        int x, int y, int width, int height,
                        bool isVertical, int thumbStart, int thumbSize,
                        bool isMouseOver, bool /*isMouseDown*/) override
    {
        const auto pal = ShowTheme::get (currentIsDark);
        const float alpha = isMouseOver ? 0.55f : 0.32f;

        if (thumbSize > 0)
        {
            const int padding = 2;
            juce::Rectangle<float> thumb;

            if (isVertical)
                thumb = { (float)(x + padding), (float)(y + thumbStart + padding),
                          (float)(width - padding * 2), (float)(thumbSize - padding * 2) };
            else
                thumb = { (float)(x + thumbStart + padding), (float)(y + padding),
                          (float)(thumbSize - padding * 2), (float)(height - padding * 2) };

            g.setColour (pal.textSecondary.withAlpha (alpha));
            g.fillRoundedRectangle (thumb, 3.0f);
        }
    }

    int getDefaultScrollbarWidth() override { return 8; }

    //==============================================================================
    /** PopupMenu Farrago-style: nền charcoal, bo góc mềm, khoảng cách dòng rộng. */
    void drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height,
                                             const juce::PopupMenu::Options&) override
    {
        const auto pal = ShowTheme::get (currentIsDark);
        constexpr float cornerRadius = 10.0f;

        g.setColour (pal.borderSubtle);
        g.fillRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, cornerRadius);

        g.setColour (findColour (juce::PopupMenu::backgroundColourId));
        g.fillRoundedRectangle (1.0f, 1.0f, (float) width - 2.0f, (float) height - 2.0f, cornerRadius - 1.0f);
    }

    int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override { return 6; }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override
    {
        if (isSeparator)
        {
            auto r = area.reduced (14, 0);
            r.removeFromTop (r.getHeight() / 2);
            g.setColour (findColour (juce::PopupMenu::textColourId).withAlpha (0.22f));
            g.fillRect (r.removeFromTop (1));
            return;
        }

        auto textColourToUse = (textColour != nullptr ? *textColour
                                                      : findColour (juce::PopupMenu::textColourId));
        auto r = area.reduced (6, 2);

        if (isHighlighted && isActive)
        {
            g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
            g.fillRoundedRectangle (r.toFloat(), 5.0f);
            textColourToUse = findColour (juce::PopupMenu::highlightedTextColourId);
        }

        g.setColour (textColourToUse.withMultipliedAlpha (isActive ? 1.0f : 0.45f));
        g.setFont (getPopupMenuFont());

        auto content = r.reduced (10, 0);

        if (isTicked)
        {
            const auto tickArea = content.removeFromLeft (16).toFloat().reduced (1.0f);
            const auto tickCol = (isHighlighted && isActive) ? juce::Colours::white : textColourToUse;
            showcontrol::icons::paintCheckmark (g, tickArea, tickCol);
            content.removeFromLeft (4);
        }

        if (hasSubMenu)
        {
            auto arrowH = 0.55f * getPopupMenuFont().getAscent();
            auto x = (float) content.removeFromRight ((int) arrowH).getX();
            auto halfH = (float) content.getCentreY();

            juce::Path path;
            path.startNewSubPath (x, halfH - arrowH * 0.5f);
            path.lineTo (x + arrowH * 0.55f, halfH);
            path.lineTo (x, halfH + arrowH * 0.5f);
            g.strokePath (path, juce::PathStrokeType (1.6f));
        }

        g.drawFittedText (text, content, juce::Justification::centredLeft, 1);

        if (shortcutKeyText.isNotEmpty())
        {
            auto f2 = getPopupMenuFont();
            f2.setHeight (f2.getHeight() * 0.82f);
            g.setFont (f2);
            g.drawText (shortcutKeyText, r.reduced (10, 0), juce::Justification::centredRight, true);
        }

        juce::ignoreUnused (icon);
    }

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override
    {
        if (isSeparator)
        {
            idealWidth = 180;
            idealHeight = 10;
            return;
        }

        auto font = getPopupMenuFont();
        idealHeight = juce::jmax (36, standardMenuItemHeight > 0 ? standardMenuItemHeight : 36);
        idealWidth = juce::GlyphArrangement::getStringWidthInt (font, text) + 56;
    }

private:
    bool currentIsDark = true;

    void applyPalette (bool dark);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShowControlLookAndFeel)
};

/** Label inline sửa tên track — message thread only. */
class TrackNameEditLabel : public juce::Label
{
public:
    TrackNameEditLabel()
    {
        setEditable (true, true, false);
        setMinimumHorizontalScale (1.0f);
        setJustificationType (juce::Justification::centredLeft);
    }

protected:
    juce::TextEditor* createEditorComponent() override
    {
        auto* ed = juce::Label::createEditorComponent();
        ed->setComponentID ("showcue-inline-list-name-editor");
        ed->setReturnKeyStartsNewLine (false);
        ed->setMultiLine (false);
        ed->setScrollbarsShown (false);
        ed->setPopupMenuEnabled (false);
        return ed;
    }
};

/** Màu vẽ paint() — đọc động từ LookAndFeel, không hardcode hex trong panel. */
namespace showcontrol::ui
{
    struct ThemePaintColours
    {
        bool isDark = true;
        juce::Colour windowBg;
        juce::Colour panelBg;
        juce::Colour centerBg;
        juce::Colour borderSubtle;
        juce::Colour textPrimary;
        juce::Colour textSecondary;
        juce::Colour textMuted;
        juce::Colour accent;
        juce::Colour success;
        juce::Colour rowSelected;
        juce::Colour listRowBg;
        juce::Colour panelElevated;

        static ThemePaintColours read (const juce::Component& c) noexcept
        {
            const auto& laf = c.getLookAndFeel();
            bool dark = true;

            if (auto* showLaf = dynamic_cast<const ShowControlLookAndFeel*> (&laf))
                dark = showLaf->isDarkMode();

            const auto pal = ShowTheme::get (dark);

            ThemePaintColours cols;
            cols.isDark         = dark;
            cols.windowBg       = laf.findColour (juce::ResizableWindow::backgroundColourId);
            cols.panelBg        = laf.findColour (ShowControlLookAndFeel::panelBackgroundColourId);
            cols.textPrimary    = laf.findColour (juce::Label::textColourId);
            cols.textSecondary  = laf.findColour (ShowControlLookAndFeel::textSecondaryColourId);
            cols.listRowBg      = laf.findColour (juce::ListBox::backgroundColourId);
            cols.accent         = laf.findColour (ShowControlLookAndFeel::accentColourId);
            cols.success        = laf.findColour (ShowControlLookAndFeel::successColourId);
            cols.centerBg       = pal.centerBg;
            cols.borderSubtle   = pal.borderSubtle;
            cols.textMuted      = pal.textMuted;
            cols.rowSelected    = pal.rowSelected;
            cols.panelElevated  = pal.panelElevated;
            return cols;
        }
    };
}
