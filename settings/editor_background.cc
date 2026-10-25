// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "editor_app.h"

#include "background_dialog.h"

namespace cxxime::settings {

void EditorApp::choose_background() {
    const bool was_previewing = candPreviewVisible_;
    hide_candidate_preview_window();
    auto preview_config = build_cand_preview_config();
    preview_config.inline_preedit = SendMessageW(hInlinePreedit_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    preview_config.input_mode = SendMessageW(hInputModeWubi_, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1
                                : SendMessageW(hInputModeMixed_, BM_GETCHECK, 0, 0) == BST_CHECKED
                                    ? 2
                                    : 0;
    if (auto edited = edit_background(hwnd_, preview_config)) {
        config_.background = edited->config;
        backgroundPreviews_[0] = edited->horizontal;
        backgroundPreviews_[1] = edited->vertical;
        for (const auto& resource : edited->resources) {
            backgroundDrafts_.add(resource);
        }
        show_candidate_preview_window();
        backgroundDrafts_.discard_unused(config_.background);
    } else if (was_previewing) {
        show_candidate_preview_window();
    }
}

bool EditorApp::handle_background_command(int control_id, int notification) {
    if (control_id == 1122 && notification == BN_CLICKED) {
        choose_background();
        return true;
    }
    return false;
}
} // namespace cxxime::settings
