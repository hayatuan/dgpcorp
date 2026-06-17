#pragma once
#include <JuceHeader.h>

namespace showcontrol::localization
{
/** Bọc UTF-8 an toàn — mọi chuỗi Tiếng Việt trong UI phải đi qua đây. */
inline juce::String tr (const char* utf8Text)
{
    return juce::LocalisedStrings::translateWithCurrentMappings (juce::String::fromUTF8 (utf8Text));
}

/** Tên mặc định khi bấm Thêm BGM / Thêm Cue trên Sidebar. */
inline juce::String defaultBgmListName() { return tr (u8"Danh sách BGM"); }
inline juce::String defaultCueListName() { return tr (u8"Danh sách Cue"); }

/** Từ điển EN — toàn bộ key/value dùng tiền tố u8 để giữ UTF-8 thuần. */
inline juce::String getEnglishDictionary()
{
    static const juce::String dict = juce::String::fromUTF8 (
        u8"\"QUYỀN HỆ THỐNG\" = \"System Permissions\"\n"
        u8"\"Tìm kiếm...\" = \"Search...\"\n"
        u8"\"🔍 Tìm kiếm...\" = \"🔍 Search...\"\n"
        u8"\"Phần mềm phát nhạc sự kiện\" = \"Event Audio Playout Software\"\n"
        u8"\"Thiết kế & lập trình: Hayatuan\" = \"Designed & developed by Hayatuan\"\n"
        u8"\"© 2026 Hayatuan. All rights reserved.\" = \"© 2026 Hayatuan. All rights reserved.\"\n"
        u8"\"KHÔNG CÓ BÀI HÁT ĐANG PHÁT\" = \"NO TRACK PLAYING\"\n"
        u8"\"DANH SÁCH NHẠC NỀN BGM\" = \"BGM BACKGROUND MUSIC\"\n"
        u8"\"DANH SÁCH CUE KỊCH BẢN\" = \"CUE PLAYLIST TRACKS\"\n"
        u8"\"TÊN BÀI HÁT BGM\" = \"BGM TRACK NAME\"\n"
        u8"\"TÊN BÀI HÁT KỊCH BẢN BGM\" = \"BGM TRACK NAME\"\n"
        u8"\"THỜI GIAN CÒN LẠI\" = \"TIME REMAINING\"\n"
        u8"\"TỔNG THỜI LƯỢNG\" = \"TOTAL DURATION\"\n"
        u8"\"CÒN LẠI\" = \"TIME REMAINING\"\n"
        u8"\"THỜI LƯỢNG\" = \"TOTAL DURATION\"\n"
        u8"\"Đóng\" = \"Close\"\n"
        u8"\"Đặt lại Quyền\" = \"Reset Permissions\"\n"
        u8"\"Audio Hardware Access\" = \"Audio Hardware Access\"\n"
        u8"\"Local Network Access\" = \"Local Network Access\"\n"
        u8"\"Truy cập soundcard / mixer sân khấu để phát CUE & BGM không độ trễ trên FOH.\" = \"Access stage soundcards / mixers for latency-free CUE & BGM playback at FOH.\"\n"
        u8"\"Kết nối Internet & mạng nội bộ để kiểm tra cập nhật từ GitHub và đồng bộ show.\" = \"Connect to Internet & local network for GitHub updates and show synchronization.\"\n"
        u8"\"Cấp quyền ngay\" = \"Grant Access Now\"\n"
        u8"\"Enabled\" = \"Enabled\"\n"
        u8"\"Cảnh báo\" = \"Warning\"\n"
        u8"\"Để phần mềm vận hành tối ưu nhất, chúng tôi khuyến nghị kích hoạt đầy đủ các quyền này.\" = \"For optimal operation, we recommend enabling all of these permissions.\"\n"
        u8"\"Chế độ hiển thị\" = \"Appearance Mode\"\n"
        u8"\"Theo hệ thống\" = \"Match System\"\n"
        u8"\"Sáng\" = \"Light\"\n"
        u8"\"Tối\" = \"Dark\"\n"
        u8"\"Chọn giao diện cho toàn bộ ứng dụng. Thay đổi có hiệu lực ngay lập tức.\" = \"Choose the appearance for the entire app. Changes take effect immediately.\"\n"
        u8"\"Ngôn ngữ\" = \"Language\"\n"
        u8"\"Mặc định hệ thống\" = \"System Language\"\n"
        u8"\"Tiếng Việt\" = \"Tiếng Việt\"\n"
        u8"\"English\" = \"English\"\n"
        u8"\"Cài đặt\" = \"Preferences\"\n"
        u8"\"Hoàn tác\" = \"Undo\"\n"
        u8"\"Làm lại\" = \"Redo\"\n"
        u8"\"Chỉnh sửa\" = \"Edit\"\n"
        u8"\"Hoàn tác thao tác gần nhất\" = \"Undo the last action\"\n"
        u8"\"Làm lại thao tác vừa hoàn tác\" = \"Redo the last undone action\"\n"
        u8"\"Nạp bài hát\" = \"Import Tracks\"\n"
        u8"\"Nhân bản track\" = \"Duplicate Track\"\n"
        u8"\"Chỉnh Inspector\" = \"Edit Inspector\"\n"
        u8"\"Đổi màu\" = \"Change Color\"\n"
        u8"\"Đổi tên\" = \"Rename\"\n"
        u8"\"Âm thanh\" = \"Audio\"\n"
        u8"\"Giao diện\" = \"Appearance\"\n"
        u8"\"Quyền\" = \"Permissions\"\n"
        u8"\"Phiên bản \" = \"Version \"\n"
        u8"\"Ủng hộ phát triển\" = \"Support Development\"\n"
        u8"\"Gửi phản hồi Beta\" = \"Send Beta Feedback\"\n"
        u8"\"Thêm BGM\" = \"Add BGM\"\n"
        u8"\"Thêm Cue\" = \"Add Cue\"\n"
        u8"\"Danh sách BGM\" = \"New BGM List\"\n"
        u8"\"Danh sách Cue\" = \"New Cue List\"\n"
        u8"\"Lặp lại bài\" = \"Track Loop\"\n"
        u8"\"Loop bài\" = \"Track Loop\"\n"
        u8"\"Lặp lại cue\" = \"Loop Cue\"\n"
        u8"\"Loop cue\" = \"Loop Cue\"\n"
        u8"\"Equalizer\" = \"Equalizer\"\n"
        u8"\"Đồng bộ âm lượng\" = \"Sync Volume\"\n"
        u8"\"Đồng bộ cả list\" = \"Sync Entire Playlist\"\n"
        u8"\"Đầu ra Audio (Bus):\" = \"Audio Output (Bus):\"\n"
        u8"\"Âm lượng:\" = \"Volume:\"\n"
        u8"\"Cấu hình Audio\" = \"Audio Configuration\"\n"
        u8"\"Đặt tên Output Bus\" = \"Output Bus Naming\"\n"
        u8"\"ĐẶT TÊN OUTPUT BUS (OUTPUT ROUTING)\" = \"OUTPUT BUS NAMING (OUTPUT ROUTING)\"\n"
        u8"\"Chỉnh sửa tên Bus...\" = \"Edit Bus Names...\"\n"
        u8"\"Monitor\" = \"Monitor\"\n"
        u8"\"Đầu ra FOH Chính\" = \"Main FOH\"\n"
        u8"\"Monitor Sân Khấu\" = \"Stage Monitor\"\n"
        u8"\"AUX 2 (Ch 5-6)\" = \"AUX 2 (Ch 5-6)\"\n"
        u8"\"AUX 3 (Ch 7-8)\" = \"AUX 3 (Ch 7-8)\"\n"
        u8"\"Xóa\" = \"Delete\"\n"
        u8"\"Đặt làm điểm Play chính\" = \"Set as Primary Playback Point\"\n"
        u8"\"Rê chuột để kéo thả di chuyển vị trí kịch bản\" = \"Drag and drop to reorder event script\"\n"
        u8"\"Kích hoạt tạm dừng hoặc phát tiếp tục tại chỗ\" = \"Toggle Pause or Resume at current position\"\n"
        u8"\"Đồng bộ mức âm lượng chuẩn LUFS toàn danh sách\" = \"Synchronize standard LUFS volume across playlist\"\n"
        u8"\"DANH SÁCH KỊCH BẢN CUE TRỐNG\" = \"CUE EVENT PLAYLIST IS EMPTY\"\n"
        u8"\"Kéo thả file nhạc vào đây để thiết lập show\" = \"Drag and drop audio files here to setup show\"\n"
        u8"\"Tín hiệu đầu ra\" = \"Output Signal\"\n"
        u8"\"Cấu hình đường Bus\" = \"Bus Routing Configuration\"\n"
        u8"\"Bạn có chắc chắn muốn xóa mục này khỏi kịch bản?\" = \"Are you sure you want to delete this item from the script?\"\n"
        u8"\"Đổi tên\" = \"Rename\"\n"
        u8"\"Đổi tên bài hát\" = \"Rename Track\"\n"
        u8"\"Nhập tên bài hát mới\" = \"Enter new track name\"\n"
        u8"\"Nhân bản\" = \"Duplicate\"\n"
        u8"\"Thêm âm thanh...\" = \"Add Sounds...\"\n"
        u8"\"Thay đổi file nhạc...\" = \"Replace Audio File...\"\n"
        u8"\"Chỉnh sửa (Trim Editor)...\" = \"Edit (Trim Editor)...\"\n"
        u8"\"Mở vị trí tệp...\" = \"Reveal in Finder...\"\n"
        u8"\"Reset Fade về mặc định (0 ms)\" = \"Reset Fade to Default (0 ms)\"\n"
        u8"\"TÊN CUE KỊCH BẢN\" = \"CUE TRACK NAME\"\n"
        u8"\"ĐÃ CHẠY\" = \"ELAPSED TIME\"\n"
        u8"\"KHÔNG CÓ CUE\" = \"NO CUE ACTIVE\"\n"
        u8"\"CUE đang phát ở chế độ lặp lại\" = \"CUE is looping\"\n"
        u8"\"BGM đang phát ở chế độ lặp lại\" = \"BGM is looping\"\n"
        u8"\"Luôn hiện trên cùng (Ghim Top)\" = \"Always on Top (Pin)\"\n"
        u8"\"Toàn màn hình (Full Screen)\" = \"Full Screen\"\n"
        u8"\"Ẩn/Hiện Sidebar\" = \"Show/Hide Sidebar\"\n"
        u8"\"Ẩn/Hiện Inspector\" = \"Show/Hide Inspector\"\n"
        u8"\"Bật/Tắt màn hình phụ giám sát đếm ngược dành cho sân khấu và đạo diễn kịch bản.\" = \"Toggle the secondary countdown monitor for stage and show director.\"\n"
        u8"\"Cài đặt — Thiết bị âm thanh, Output Bus và Giao diện\" = \"Settings — Audio device, Output Bus and Appearance\"\n"
        u8"\"Đồng bộ mức Gain chuẩn hóa dựa trên cấu hình phân tích RMS hoặc LUFS.\" = \"Sync normalized gain based on RMS or LUFS analysis settings.\"\n"
        u8"\"Áp dụng cấu hình cân bằng âm lượng hiện tại cho toàn bộ các track trong danh sách kịch bản.\" = \"Apply current volume balance settings to all tracks in the playlist.\"\n"
        u8"\"Chuyển chế độ đo âm lượng theo công suất trung bình tín hiệu điện toán RMS.\" = \"Switch loudness measurement to RMS average signal power.\"\n"
        u8"\"Chuyển chế độ đo âm lượng theo thuật toán cảm nhận tai người chuẩn phát thanh LUFS.\" = \"Switch loudness measurement to broadcast-standard LUFS.\"\n"
        u8"\"Bật/Tắt chế độ phát lặp lại tuần hoàn cho riêng track nhạc hoặc ô PAD này.\" = \"Toggle loop playback for this track or PAD.\"\n"
        u8"\"Mở bảng cấu hình bộ lọc tần số và cân bằng âm sắc cho track.\" = \"Open frequency filter and tone balance settings for this track.\"\n"
        u8"\"Thời gian lịm tiếng nhỏ đến to khi bắt đầu phát nhạc (tính bằng mili-giây).\" = \"Fade-in duration when playback starts (milliseconds).\"\n"
        u8"\"Thời gian lịm tiếng to đến nhỏ khi chủ động dừng phát nhạc (tính bằng mili-giây).\" = \"Fade-out duration when stopping playback (milliseconds).\"\n"
        u8"\"Định tuyến đầu ra stereo của pad/track ra Main FOH hoặc AUX trên soundcard/Dante. Nếu thiết bị không đủ kênh, tự động fallback về Main FOH (Ch 1-2).\" = \"Route stereo output to Main FOH or AUX on soundcard/Dante. Falls back to Main FOH (Ch 1-2) if channels are unavailable.\"\n"
        u8"\"Chuyển sang dạng danh sách BGM\" = \"Switch to BGM List View\"\n"
        u8"\"Chuyển sang dạng lưới CUE\" = \"Switch to CUE Grid View\"\n"
        u8"\"Lặp lại danh sách\" = \"Loop Playlist\"\n"
        u8"\"Xóa danh sách phát\" = \"Delete Playlist\"\n"
        u8"\"Danh sách CUE trống. Hãy kéo thả file âm thanh vào đây để tự động cấu hình các ô PAD biểu diễn.\" = \"CUE list is empty. Drag and drop audio files here to auto-configure performance PADs.\"\n"
        u8"\"Danh sách BGM trống. Hãy kéo thả file nhạc nền vào phân vùng này để thiết lập danh sách phát.\" = \"BGM list is empty. Drag background music files here to build the playlist.\"\n"
        u8"\"Xoá\" = \"Delete\"\n"
        u8"\"Hủy\" = \"Cancel\"\n"
        u8"\"Xác nhận xóa bài hát khỏi kịch bản?\" = \"Confirm track deletion from script?\"\n"
        u8"\"Bộ cân bằng tần số (Equalizer)\" = \"Frequency Equalizer (EQ)\"\n"
        u8"\"Cắt gọt âm thanh\" = \"Audio Trim Editor\"\n"
        u8"\"Điểm đầu\" = \"Trim Start\"\n"
        u8"\"Điểm cuối\" = \"Trim End\"\n"
        u8"\"Bỏ qua (Bypass)\" = \"Bypass\"\n"
        u8"\"Tệp\" = \"File\"\n"
        u8"\"Chỉnh sửa\" = \"Edit\"\n"
        u8"\"Trợ giúp\" = \"Help\"\n"
        u8"\"Thoát\" = \"Quit\"\n"
        u8"\"Cài đặt\" = \"Settings\"\n"
        u8"\"About ShowCue\" = \"About ShowCue\"\n"
        u8"\"Giới thiệu ứng dụng\" = \"About the application\"\n"
        u8"\"Application\" = \"Application\"\n"
        u8"\"Kiểm tra cập nhật...\" = \"Check for Updates...\"\n"
        u8"\"Kiểm tra cập nhật\" = \"Check for Updates\"\n"
        u8"\"Đang kiểm tra cập nhật...\" = \"Checking for updates...\"\n"
        u8"\"Kiểm tra phiên bản mới trên máy chủ\" = \"Check for a new version on the server\"\n"
        u8"\"Cập nhật phần mềm\" = \"Software Update\"\n"
        u8"\"Đã có phiên bản mới. Bạn có muốn tải về không?\" = \"A new version is available. Would you like to download it now?\"\n"
        u8"\"Tải về ngay\" = \"Download Now\"\n"
        u8"\"Để sau\" = \"Later\"\n"
        u8"\"Cập nhật ứng dụng\" = \"Application Update\"\n"
        u8"\"Kiểm tra phiên bản mới từ GitHub và mở trang tải về.\" = \"Check for a new version on GitHub and open the download page.\"\n"
        u8"\"Ứng dụng của bạn đã là phiên bản mới nhất!\" = \"Your application is up to date!\"\n"
        u8"\"Không thể kết nối máy chủ cập nhật.\nVui lòng thử lại sau.\" = \"Unable to reach the update server.\nPlease try again later.\"\n"
        u8"\"Dữ liệu cập nhật không hợp lệ.\" = \"Update data is invalid.\"\n"
        u8"\"Cài đặt...\" = \"Preferences...\"\n"
        u8"\"Mở hộp thoại cấu hình hệ thống\" = \"Open system preferences dialog\"\n"
        u8"\"General\" = \"General\"\n"
        u8"\"Bật EQ\" = \"Enable EQ\"\n"
        u8"\"Reset mặc định\" = \"Reset to Default\"\n"
        u8"\"Kéo nút + theo chiều dọc · thấp → cao: HP → LS → P1 → P2 → HS → LP\" = \"Drag + nodes vertically · low → high: HP → LS → P1 → P2 → HS → LP\"\n"
        u8"\"Kéo 2 marker vàng/đỏ để chọn điểm IN/OUT. Lăn chuột để zoom. Kéo cạnh cửa sổ để mở rộng.\" = \"Drag yellow/red markers to set IN/OUT points. Scroll to zoom. Drag window edge to widen.\"\n"
        u8"\"IN/OUT: kéo marker vàng/đỏ. Cắt nhạc: quét chọn vùng đỏ hoặc đặt điểm cắt tại playhead. Lăn chuột để zoom.\" = \"IN/OUT: drag yellow/red markers. Cut audio: drag to select red region or set cut points at playhead. Scroll to zoom.\"\n"
        u8"\"Đầu cắt\" = \"Cut Start\"\n"
        u8"\"Cuối cắt\" = \"Cut End\"\n"
        u8"\"Xóa chọn\" = \"Clear Selection\"\n"
        u8"\"CẮT & XÓA\" = \"CUT & DELETE\"\n"
        u8"\"Hoàn tác\" = \"Undo\"\n"
        u8"\"Làm lại\" = \"Redo\"\n"
        u8"\"Cắt & xóa đoạn âm thanh\" = \"Cut & Delete Audio Region\"\n"
        u8"\"Xóa vĩnh viễn đoạn đã chọn (%TIME%). File gốc không bị xóa — bản chỉnh được lưu riêng. Có thể Hoàn tác sau khi cắt.\" = \"Permanently remove the selected region (%TIME%). The original file is kept — edits are saved separately. You can Undo after cutting.\"\n"
        u8"\"Cắt\" = \"Cut\"\n"
        u8"\"ĐẶT LẠI\" = \"RESET\"\n"
        u8"\"XÁC NHẬN & ĐÓNG\" = \"CONFIRM & CLOSE\"\n"
        u8"\"Đừng hỏi lại lần này\" = \"Don't ask again\"\n"
        u8"\"Xóa bài hát\" = \"Delete Track\"\n"
        u8"\"Xóa nhiều bài hát\" = \"Delete Multiple Tracks\"\n"
        u8"\"Hành động này không thể hoàn tác.\" = \"This action cannot be undone.\"\n"
        u8"\"Cài đặt nâng cao...\" = \"Advanced Settings...\"\n"
        u8"\"Quản lý đồng bộ âm lượng\" = \"Loudness Manager\"\n"
        u8"\"Bật đồng bộ âm lượng\" = \"Enable Volume Sync\"\n"
        u8"\"Safe mode (chống clip)\" = \"Safe Mode (Anti-clip)\"\n"
        u8"\"A/B nghe bản gốc\" = \"A/B Compare Original\"\n"
        u8"\"Preview toàn list (trước / sau)\" = \"Full List Preview (Before / After)\"\n"
        u8"\"Tên\" = \"Name\"\n"
        u8"\"Trước\" = \"Before\"\n"
        u8"\"Sau\" = \"After\"\n"
        u8"\"Áp dụng toàn bộ list\" = \"Apply to Entire List\"\n"
        u8"\"Mở hộp thoại quản lý đồng bộ âm lượng nâng cao và áp dụng tức thì.\" = \"Open the advanced loudness manager with live apply.\"\n"
        u8"\"Tùy chỉnh\" = \"Custom\"\n"
        u8"\"Thoại / MC\" = \"Speech / MC\"\n"
        u8"\"Tổng quát\" = \"General\"\n"
        u8"\"Đang đo…\" = \"Measuring…\"\n"
        u8"\"Bật/tắt EQ cho track này\" = \"Enable or disable EQ for this track\"\n"
        u8"\"Live Show (-16 LUFS)\" = \"Live Show (-16 LUFS)\"\n"
        u8"\"Speech / VO (-20 LUFS)\" = \"Speech / VO (-20 LUFS)\"\n"
        u8"\"Music / Stream (-14 LUFS)\" = \"Music / Stream (-14 LUFS)\"\n"
        u8"\"Ballad / Acoustic\" = \"Ballad / Acoustic\"\n"
        u8"\"SFX / Stinger\" = \"SFX / Stinger\"\n"
        u8"\"Chưa đo\" = \"Not measured\"\n"
        u8"\" · Peak \" = \" · Peak \"\n"
        u8"\" · Gain \" = \" · Gain \"\n"
        u8"\" · LUFS sync \" = \" · LUFS sync \"\n");

    return dict;
}

inline bool isSystemLanguageVietnamese() noexcept
{
    return juce::SystemStats::getUserLanguage().startsWithIgnoreCase ("vi");
}

inline void applyEnglishMappings() noexcept
{
    juce::LocalisedStrings::setCurrentMappings (
        new juce::LocalisedStrings (getEnglishDictionary(), false));
}

inline void applyVietnameseMappings() noexcept
{
    juce::LocalisedStrings::setCurrentMappings (nullptr);
}

/** 0 = Match System, 1 = Tiếng Việt, 2 = English. */
inline void setAppLanguage (int languageIndex) noexcept
{
    switch (juce::jlimit (0, 2, languageIndex))
    {
        case 0:
            if (isSystemLanguageVietnamese())
                applyVietnameseMappings();
            else
                applyEnglishMappings();
            break;

        case 1:
            applyVietnameseMappings();
            break;

        default:
            applyEnglishMappings();
            break;
    }
}

} // namespace showcontrol::localization
