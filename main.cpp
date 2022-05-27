#include <cstdarg>
#include <cstdio>
#include <stdint.h>
#include <stdbool.h>
#include <malloc.h>

#include <coreinit/screen.h>
#include <coreinit/cache.h>
#include <vpad/input.h>
#include <whb/proc.h>

#include <iosuhax.h>
#include <iosuhax_disc_interface.h>

size_t tvBufferSize;
size_t drcBufferSize;
void* tvBuffer;
void* drcBuffer;

int fsaFd;
int res;

void printToScreen(int x, int y, const char *str, ...) {
    char *tmp = nullptr;

    va_list va;
    va_start(va, str);
    if ((vasprintf(&tmp, str, va) >= 0) && (tmp != nullptr)) {    
        OSScreenPutFontEx(SCREEN_TV, x, y, tmp);
        OSScreenPutFontEx(SCREEN_DRC, x, y, tmp);
    }

    va_end(va);
    if (tmp != nullptr)
        free(tmp);
}

void flipBuffers() {
    DCFlushRange(tvBuffer, tvBufferSize);
    DCFlushRange(drcBuffer, drcBufferSize);

    OSScreenFlipBuffersEx(SCREEN_TV);
    OSScreenFlipBuffersEx(SCREEN_DRC);
}

void clearBuffers() {
    OSScreenClearBufferEx(SCREEN_TV, 0x00000000);
    OSScreenClearBufferEx(SCREEN_DRC, 0x00000000);
}

void hideOrUnhideUSB(uint8_t *mbr) {
    if(mbr[511] == 0xAA) {
        mbr[511]++;
        IOSUHAX_usb_disc_interface.writeSectors(0, 1, mbr);
    } else if(mbr[511] == 0xAB) {
        mbr[511]--;
        IOSUHAX_usb_disc_interface.writeSectors(0, 1, mbr);
    } else
        return;
}

int main() {
    WHBProcInit();

    OSScreenInit();

    tvBufferSize = OSScreenGetBufferSizeEx(SCREEN_TV);
    drcBufferSize = OSScreenGetBufferSizeEx(SCREEN_DRC);

    tvBuffer = memalign(0x100, tvBufferSize);
    drcBuffer = memalign(0x100, drcBufferSize);

    if (!tvBuffer || !drcBuffer) {

        if (tvBuffer) free(tvBuffer);
        if (drcBuffer) free(drcBuffer);

        OSScreenShutdown();
        WHBProcShutdown();

        return 1;
    }

    OSScreenSetBufferEx(SCREEN_TV, tvBuffer);
    OSScreenSetBufferEx(SCREEN_DRC, drcBuffer);

    OSScreenEnableEx(SCREEN_TV, true);
    OSScreenEnableEx(SCREEN_DRC, true);

    int res = IOSUHAX_Open(NULL);
    if (res < 0) {
        printToScreen(0, 0, "IOSUHAX_Open failed");
        flipBuffers();
        WHBProcShutdown();
        return 1;
    }

    fsaFd = IOSUHAX_FSA_Open();
    if (fsaFd < 0) {
        printToScreen(0, 0, "IOSUHAX_FSA_Open failed.");
        flipBuffers();
        WHBProcShutdown();
        return 1;
    }

    uint8_t *mbr = (uint8_t*)malloc(512);
    IOSUHAX_usb_disc_interface.startup();
    IOSUHAX_usb_disc_interface.readSectors(0, 1, mbr);

    VPADStatus status;
    VPADReadError error;

    while(WHBProcIsRunning()) {
        VPADRead(VPAD_CHAN_0, &status, 1, &error);
        clearBuffers();

        printToScreen(0, 0, "MBR: %X", mbr[511]);
        if(mbr[511] == 0xAA) {
            printToScreen(0, 1, "Current state: not hidden");
            printToScreen(0, 2, "Press A to hide the USB");
        } else if(mbr[511] == 0xAB) {
            printToScreen(0, 1, "Current state: hidden");
            printToScreen(0, 2, "Press A to show the USB");
        } else
            printToScreen(0, 1, "Unknown MBR value");

        flipBuffers();

        if (status.trigger & VPAD_BUTTON_A)
            hideOrUnhideUSB(mbr);
    }

    if (tvBuffer) free(tvBuffer);
    if (drcBuffer) free(drcBuffer);

    OSScreenShutdown();
    WHBProcShutdown();
    IOSUHAX_FSA_Close(fsaFd);
    IOSUHAX_Close();
    return 0;
}
