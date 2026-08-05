// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/symbol_processor.h>

#include <windows.h>

namespace cxxime {

bool SymbolProcessor::is_active(const Context& context) {
    return !context.pinyin_buffer.empty() && context.pinyin_buffer.front() == '/';
}

bool SymbolProcessor::is_trigger(const KeyEvent& event) {
    return !event.is_key_up &&
           event.keycode == VK_OEM_2 &&
           !event.is_shift() &&
           !event.is_ctrl() &&
           !event.is_alt();
}

bool SymbolProcessor::edit_preedit(const KeyEvent& event, Context& context) {
    switch (event.keycode) {
    case VK_BACK:
        context.erase_preedit_before_cursor();
        if (context.pinyin_buffer.empty()) {
            context.reset();
        }
        return true;
    case VK_DELETE:
        context.erase_preedit_at_cursor();
        return true;
    case VK_LEFT:
        if (context.preedit_cursor() > 1) {
            context.move_preedit_cursor_left();
        }
        return true;
    case VK_RIGHT:
        context.move_preedit_cursor_right();
        return true;
    case VK_HOME:
        while (context.preedit_cursor() > 1) {
            context.move_preedit_cursor_left();
        }
        return true;
    case VK_END:
        context.move_preedit_cursor_end();
        return true;
    default:
        return false;
    }
}

ProcessResult SymbolProcessor::select_candidate(Context& context, int index) {
    if (index < 0 || index >= context.selectable_candidate_count()) {
        return ProcessResult::ACCEPTED;
    }

    const Candidate& candidate = context.candidates.candidates[index];
    if (context.pinyin_buffer == "/" && candidate.source == CandidateSource::kSymbol &&
        candidate.code.size() > 1 && candidate.code.front() == '/') {
        context.set_preedit(candidate.code);
        context.candidates = {};
        context.reset_pagination();
        return ProcessResult::ACCEPTED;
    }

    context.candidates.highlighted = index;
    context.committed_text = candidate.text;
    return ProcessResult::COMMITTED;
}

ProcessResult SymbolProcessor::process_key(const KeyEvent& event, Context& context,
                                           bool allow_trigger) const {
    if (event.is_key_up) {
        return ProcessResult::REJECTED;
    }

    if (!is_active(context)) {
        if (!allow_trigger || !is_trigger(event) || context.is_composing()) {
            return ProcessResult::REJECTED;
        }
        context.set_preedit("/");
        context.candidates = {};
        context.reset_pagination();
        return ProcessResult::ACCEPTED;
    }

    if (event.keycode == VK_ESCAPE) {
        context.reset();
        return ProcessResult::ACCEPTED;
    }
    if (edit_preedit(event, context)) {
        return ProcessResult::ACCEPTED;
    }

    if (event.keycode == VK_SPACE) {
        if (!context.candidates.candidates.empty()) {
            const int index =
                context.candidates.highlighted >= 0 ? context.candidates.highlighted : 0;
            return select_candidate(context, index);
        }
        context.reset();
        return ProcessResult::ACCEPTED;
    }

    if (event.keycode == VK_UP || event.keycode == VK_DOWN) {
        if (!context.candidates.candidates.empty()) {
            if (event.keycode == VK_DOWN) {
                context.move_to_next_candidate();
            } else {
                context.move_to_previous_candidate();
            }
        }
        return ProcessResult::ACCEPTED;
    }

    if (event.keycode == VK_RETURN) {
        context.committed_text = context.pinyin_buffer;
        context.set_commit_source(CommitSource::kRawCodePreserveCase);
        context.clear_preedit();
        context.candidates = {};
        context.reset_pagination();
        return ProcessResult::COMMITTED;
    }

    if (event.keycode >= '1' && event.keycode <= '9') {
        if (!context.candidates.candidates.empty()) {
            const int index = static_cast<int>(event.keycode - '1');
            return select_candidate(context, index);
        }
        context.insert_preedit(static_cast<char>(event.keycode));
        return ProcessResult::ACCEPTED;
    }

    const bool shortcut_page_up = event.keycode == VK_OEM_MINUS && !event.is_shift();
    const bool shortcut_page_down = event.keycode == VK_OEM_PLUS && !event.is_shift();
    if (event.keycode == VK_PRIOR || event.keycode == VK_NEXT || shortcut_page_up ||
        shortcut_page_down) {
        if (!context.candidates.candidates.empty()) {
            if (event.keycode == VK_NEXT || shortcut_page_down) {
                context.move_to_next_page();
            } else {
                context.move_to_previous_page();
            }
        }
        return ProcessResult::ACCEPTED;
    }

    if (event.keycode >= 'A' && event.keycode <= 'Z') {
        context.insert_preedit(static_cast<char>(event.keycode - 'A' + 'a'));
        return ProcessResult::ACCEPTED;
    }

    return ProcessResult::REJECTED;
}

} // namespace cxxime
