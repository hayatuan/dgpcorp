#pragma once

#include <cstdint>

/** Trạng thái cue theo mô hình QLab (.cursorrules). */
enum class PadCueState : uint8_t
{
    empty    = 0,
    loading  = 1,
    ready    = 2,
    playing  = 3,
    paused   = 4,
    stopped  = 5,
    /** Fade-out stop đang chạy — khóa play/GO cho đến khi dừng hẳn. */
    stopping = 6
};
