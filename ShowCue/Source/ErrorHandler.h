#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class ErrorHandler
{
public:
    enum class Severity
    {
        Info,
        Warning,
        Error
    };

    // Hàm ghi nhận nhật ký chạy phần mềm lên hệ thống
    static void log (const juce::String& message, Severity severity = Severity::Info)
    {
        juce::String prefix = "[INFO] ";
        if (severity == Severity::Warning)     prefix = "[WARNING] ";
        else if (severity == Severity::Error)   prefix = "[ERROR] ";
        
        juce::Logger::writeToLog (prefix + message);
    }

    // Hàm tích hợp ghi log kèm bung mở hộp thoại Native chuẩn chuyên nghiệp
    static void logAndShow (const juce::String& title, const juce::String& message, Severity severity = Severity::Error)
    {
        log (title + " - " + message, severity);

        juce::MessageBoxIconType icon = juce::MessageBoxIconType::InfoIcon;
        
        // CỐ ĐỊNH CHỮ HOA: Đã sửa thành Severity::Error chuẩn chỉ triệt tiêu lỗi dòng 31
        if (severity == Severity::Warning || severity == Severity::Error)
            icon = juce::MessageBoxIconType::WarningIcon;

        juce::NativeMessageBox::showMessageBoxAsync (
            icon,
            title,
            message,
            nullptr,
            juce::ModalCallbackFunction::create ([] (int) {})
        );
    }

    // Hàm cổng phụ hỗ trợ hiển thị lỗi nhanh
    static void showWithError (const juce::String& title, const juce::String& message)
    {
        logAndShow (title, message, Severity::Error);
    }
};