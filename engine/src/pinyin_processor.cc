// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/processor.h>

#include <windows.h>

namespace cxxime {

ProcessResult PinyinProcessor::process_key(const KeyEvent& event, Context& context) {
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

    // Space: select first candidate, or dismiss if no candidates
    if (vk == VK_SPACE) {
        if (context.is_composing() &&
            context.commit_source() == CommitSource::kRawCodePreserveCase) {
            context.committed_text = context.pinyin_buffer;
            context.candidates = {};
            context.reset_pagination();
            context.clear_preedit();
            return ProcessResult::COMMITTED;
        }
        if (context.is_composing() && !context.candidates.candidates.empty()) {
            if (context.candidates.highlighted >= 0 && context.candidates.highlighted < (int)context.candidates.candidates.size()) {
                context.committed_text = context.candidates.candidates[context.candidates.highlighted].text;
                return ProcessResult::COMMITTED;
            }
        }
        if (context.is_composing()) {
            // Append mode: commit buffer text instead of discarding
            if (event.is_caps_lock() && context.caps_lock_style == AsciiModeSwitchStyle::APPEND) {
                context.committed_text = context.pinyin_buffer;
                context.set_commit_source(CommitSource::kRawCodePreserveCase);
                context.candidates = {};
                context.reset_pagination();
                context.clear_preedit();
                return ProcessResult::COMMITTED;
            }
            context.reset();
            return ProcessResult::ACCEPTED;
        }
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

    // Enter: commit pinyin as raw text
    if (vk == VK_RETURN) {
        if (context.is_composing()) {
            context.committed_text = context.pinyin_buffer;
            if (context.commit_source() == CommitSource::kRawCodePreserveCase ||
                (event.is_caps_lock() && context.caps_lock_style == AsciiModeSwitchStyle::APPEND))
                context.set_commit_source(CommitSource::kRawCodePreserveCase);
            context.clear_preedit();
            context.candidates = {};
            return ProcessResult::COMMITTED;
        }
        return ProcessResult::REJECTED;
    }

    // Number keys 1-9: select candidate by index
    if (is_digit_key(vk) && vk >= '1' && vk <= '9') {
        if (context.is_composing() && !context.candidates.candidates.empty()) {
            int index = vk - '1';
            if (index < context.selectable_candidate_count()) {
                context.candidates.highlighted = index;
                context.committed_text = context.candidates.candidates[index].text;
                return ProcessResult::COMMITTED;
            }
            return ProcessResult::ACCEPTED;
        }
        // If not composing, let the number pass through
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

    // Letter keys: append to pinyin buffer
    if (is_letter_key(vk)) {
        char ch;
        // Append mode with CapsLock: preserve original case
        if (event.is_caps_lock() && context.caps_lock_style == AsciiModeSwitchStyle::APPEND) {
            ch = static_cast<char>(vk);  // uppercase VK code
            if (event.is_shift())
                ch = static_cast<char>(vk - 'A' + 'a');  // Shift+CapsLock → lowercase
            context.set_commit_source(CommitSource::kRawCodePreserveCase);
            context.candidates = {};
            context.reset_pagination();
        } else {
            ch = static_cast<char>(vk - 'A' + 'a');  // force lowercase
        }
        context.insert_preedit(ch);
        return ProcessResult::ACCEPTED;
    }

    // Other keys: reject (pass to system)
    return ProcessResult::REJECTED;
}

} // namespace cxxime
