#include <stdio.h>
#include <switch.h>
int main(int argc, char** argv) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);
    printf("TSSM Switch toolchain hello (devkitA64 GCC %d.%d)\n", __GNUC__, __GNUC_MINOR__);
    printf("sizeof(void*)=%d  press + to exit\n", (int)sizeof(void*));
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
