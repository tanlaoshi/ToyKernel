/*
 * ToyUi.c — libToyUi
 */
#include <ToyUi.h>
#include <unistd.h>
#include <toyos/syscall.h>

int ToyUiCreateWindow(const char *Title, unsigned Width, unsigned Height) {
    return create_window(Title, Width, Height);
}

int ToyUiSetLabel(int WindowId, const char *Text) {
    return ToyGfxDamageText(WindowId, Text);
}

int ToyUiAddButton(int WindowId, int ButtonId, const char *Label) {
    long r;

    if (WindowId < 0 || ButtonId < TOY_UI_BUTTON_ID_MIN ||
        ButtonId > TOY_UI_BUTTON_ID_MAX || !Label) {
        return -1;
    }
    r = toy_ui_button(WindowId, ButtonId, Label);
    return r < 0 ? -1 : 0;
}

int ToyUiPoll(int WindowId) {
    long r;

    if (WindowId < 0) {
        return -1;
    }
    r = toy_poll_input(WindowId);
    return (int)r;
}
