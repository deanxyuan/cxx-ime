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

    // Backspace: remove last pinyin char
    if (vk == VK_BACK) {
        if (!context.pinyin_buffer.empty()) {
            context.pinyin_buffer.pop_back();
            if (context.pinyin_buffer.empty()) {
                context.candidates = {};
            }
            return ProcessResult::ACCEPTED;
        }
        return ProcessResult::REJECTED;
    }

    // Space: select first candidate
    if (vk == VK_SPACE) {
        if (context.is_composing() && !context.candidates.candidates.empty()) {
            if (context.candidates.highlighted >= 0 && context.candidates.highlighted < (int)context.candidates.candidates.size()) {
                context.committed_text = context.candidates.candidates[context.candidates.highlighted].text;
                return ProcessResult::COMMITTED;
            }
        }
        return ProcessResult::REJECTED;
    }

    // Up/Down arrows: navigate candidates
    if (vk == VK_UP || vk == VK_DOWN) {
        if (context.is_composing() && !context.candidates.candidates.empty()) {
            int count = (int)context.candidates.candidates.size();
            if (vk == VK_DOWN) {
                context.candidates.highlighted++;
                if (context.candidates.highlighted >= count)
                    context.candidates.highlighted = 0;
            } else {
                context.candidates.highlighted--;
                if (context.candidates.highlighted < 0)
                    context.candidates.highlighted = count - 1;
            }
            return ProcessResult::ACCEPTED;
        }
        return ProcessResult::REJECTED;
    }

    // Enter: commit pinyin as raw text
    if (vk == VK_RETURN) {
        if (context.is_composing()) {
            context.committed_text = context.pinyin_buffer;
            context.pinyin_buffer.clear();
            context.candidates = {};
            return ProcessResult::COMMITTED;
        }
        return ProcessResult::REJECTED;
    }

    // Number keys 1-9: select candidate by index
    if (is_digit_key(vk) && vk >= '1' && vk <= '9') {
        if (context.is_composing() && !context.candidates.candidates.empty()) {
            int index = vk - '1';
            if (index < (int)context.candidates.candidates.size()) {
                context.candidates.highlighted = index;
                context.committed_text = context.candidates.candidates[index].text;
                return ProcessResult::COMMITTED;
            }
        }
        // If not composing, let the number pass through
        return ProcessResult::REJECTED;
    }

    // Page Up / Page Down: pagination
    if (vk == VK_PRIOR || vk == VK_NEXT) {
        if (context.is_composing() && !context.candidates.candidates.empty()) {
            int total = context.candidates.total_count;
            int page_size = context.candidates.page_size;
            int max_page = (total > 0 && page_size > 0) ? (total + page_size - 1) / page_size - 1 : 0;
            if (max_page < 0) max_page = 0;

            if (vk == VK_NEXT) {  // Page Down
                if (context.page_index < max_page)
                    context.page_index++;
            } else {  // Page Up
                if (context.page_index > 0)
                    context.page_index--;
            }
            return ProcessResult::ACCEPTED;
        }
        return ProcessResult::REJECTED;
    }

    // Letter keys: append to pinyin buffer
    if (is_letter_key(vk)) {
        char ch = static_cast<char>(vk - 'A' + 'a');
        context.pinyin_buffer += ch;
        return ProcessResult::ACCEPTED;
    }

    // Other keys: reject (pass to system)
    return ProcessResult::REJECTED;
}

} // namespace cxxime
