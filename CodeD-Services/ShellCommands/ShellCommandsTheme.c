/*
 * ShellCommandsTheme.c — set effects（PR-GUI-effects）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Theme.h"

static int EffectsNameEq(const char *A, const char *B) {
    int i;

    if (!A || !B) {
        return 0;
    }
    for (i = 0; A[i] || B[i]; i++) {
        if (A[i] != B[i]) {
            return 0;
        }
    }
    return 1;
}

/* set effects minimal|low|medium|high */
static void CommandSetEffects(int Argc, char **Argv) {
    THEME_EFFECT_LEVEL Level;

    if (Argc < 2) {
        ConsoleWrite("usage: set effects minimal|low|medium|high\n");
        ConsoleWrite("  now=");
        ConsoleWrite(ThemeEffectLevelName(ThemeGetEffectLevel()));
        ConsoleWrite("\n");
        return;
    }
    if (EffectsNameEq(Argv[1], "minimal")) {
        Level = THEME_EFFECT_MINIMAL;
    } else if (EffectsNameEq(Argv[1], "low")) {
        Level = THEME_EFFECT_LOW;
    } else if (EffectsNameEq(Argv[1], "medium")) {
        Level = THEME_EFFECT_MEDIUM;
    } else if (EffectsNameEq(Argv[1], "high")) {
        Level = THEME_EFFECT_HIGH;
    } else {
        ConsoleWrite("set effects: bad level\n");
        return;
    }
    ThemeSetEffectLevel(Level);
    ThemeApply();
    ConsoleWrite("set effects: ");
    ConsoleWrite(ThemeEffectLevelName(Level));
    ConsoleWrite("\n");
}

void ShellCommandsThemeRegister(void) {
    ConsoleRegister2("set", "effects", "set effects minimal|low|medium|high",
                     CommandSetEffects);
}
