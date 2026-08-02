// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/wubi_processor.h>

#include <windows.h>

namespace cxxime {

ProcessResult WubiProcessor::process_key(const KeyEvent& event, Context& context) {
    if (event.is_key_up)
        return ProcessResult::REJECTED;

    uint32_t vk = event.keycode;

    // Escape: clear input
    if (vk == VK_ESCAPE) {
        context.reset();
        return ProcessResult::ACCEPTED;
    }

    if (context.edit_preedit(event)) {
        return ProcessResult::ACCEPTED;
    }

    // Space: select first candidate and commit
    if (vk == VK_SPACE) {
        if (context.is_composing() && !context.candidates.candidates.empty()) {
            // 五笔模式：Space 选中第一候选（不高亮状态下）
            int index = context.candidates.highlighted >= 0
                ? context.candidates.highlighted : 0;
            if (index < (int)context.candidates.candidates.size()) {
                context.candidates.highlighted = index;
                context.committed_text = context.candidates.candidates[index].text;
                return ProcessResult::COMMITTED;
            }
        }
        if (context.is_composing()) {
            context.reset();
            return ProcessResult::ACCEPTED;
        }
        // 空输入时按 Space，透传给应用
        return ProcessResult::REJECTED;
    }

    // Up/Down arrows: navigate candidates
    if (vk == VK_UP || vk == VK_DOWN) {
        if (context.is_composing() && !context.candidates.candidates.empty()) {
            if (vk == VK_DOWN) {
                context.move_to_next_candidate();
            } else {
                context.move_to_previous_candidate();
            }
            return ProcessResult::ACCEPTED;
        }
        return ProcessResult::REJECTED;
    }

    // Enter: commit raw code as text
    if (vk == VK_RETURN) {
        if (context.is_composing()) {
            context.committed_text = context.pinyin_buffer;
            context.clear_preedit();
            context.candidates = {};
            return ProcessResult::COMMITTED;
        }
        return ProcessResult::REJECTED;
    }

    // Number keys 1-9: select candidate by index
    if (vk >= '1' && vk <= '9') {
        if (context.is_composing() && !context.candidates.candidates.empty()) {
            int index = vk - '1';
            if (index < context.selectable_candidate_count()) {
                context.candidates.highlighted = index;
                context.committed_text = context.candidates.candidates[index].text;
                return ProcessResult::COMMITTED;
            }
            return ProcessResult::ACCEPTED;
        }
        return ProcessResult::REJECTED;
    }

    // Page Up / Page Down and unmodified - / =: pagination
    bool shortcut_page_up = vk == VK_OEM_MINUS && !event.is_shift() &&
                                  !event.is_ctrl() && !event.is_alt();
    bool shortcut_page_down = vk == VK_OEM_PLUS && !event.is_shift() &&
                                    !event.is_ctrl() && !event.is_alt();
    if (vk == VK_PRIOR || vk == VK_NEXT || shortcut_page_up || shortcut_page_down) {
        if (context.is_composing() && !context.candidates.candidates.empty()) {
            if (vk == VK_NEXT || shortcut_page_down) {  // Page Down
                context.move_to_next_page();
            } else {  // Page Up
                context.move_to_previous_page();
            }
            return ProcessResult::ACCEPTED;
        }
        return ProcessResult::REJECTED;
    }

    // Letter keys: append to buffer
    if (vk >= 'A' && vk <= 'Z') {
        char ch = static_cast<char>(vk - 'A' + 'a');
        context.insert_preedit(ch);
        return ProcessResult::ACCEPTED;
    }

    // Other keys: reject
    return ProcessResult::REJECTED;
}

} // namespace cxxime
