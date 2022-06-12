#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <malloc.h>

#include <coreinit/screen.h>
#include <coreinit/cache.h>
#include <vpad/input.h>
#include <whb/proc.h>

#include <iosuhax.h>
#include <iosuhax_disc_interface.h>

#include <mocha/mocha.h>
#include <mocha/fsa.h>

size_t tvBufferSize;
size_t drcBufferSize;
void* tvBuffer;
void* drcBuffer;

extern FSClient *__wut_devoptab_fs_client;

int32_t usb01Handle;
int32_t usb02Handle;

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

void hideOrUnhideUSB(uint8_t *mbr, int32_t handle) {
    if(mbr[511] == 0xAA) {
        mbr[511]++;
        FSAEx_RawWrite(__wut_devoptab_fs_client, mbr, 512, 1, 0, handle);
    } else if(mbr[511] == 0xAB) {
        mbr[511]--;
        FSAEx_RawWrite(__wut_devoptab_fs_client, mbr, 512, 1, 0, handle);
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

    if(Mocha_InitLibrary() != MOCHA_RESULT_SUCCESS) {
        printToScreen(0, 0, "Mocha_InitLibrary failed");
        flipBuffers();
        if (tvBuffer) free(tvBuffer);
        if (drcBuffer) free(drcBuffer);
        OSScreenShutdown();
        WHBProcShutdown();
        Mocha_DeInitLibrary();
        IOSUHAX_Close();
        return 1;
    }

    if(Mocha_UnlockFSClient(__wut_devoptab_fs_client) != MOCHA_RESULT_SUCCESS) {
        printToScreen(0, 0, "Mocha_UnlockFSClient failed, please update your MochaPayload");
        flipBuffers();
        if (tvBuffer) free(tvBuffer);
        if (drcBuffer) free(drcBuffer);
        OSScreenShutdown();
        WHBProcShutdown();
        Mocha_DeInitLibrary();
        IOSUHAX_Close();
        return 1;
    }

    uint8_t *usb01mbr = (uint8_t*)aligned_alloc(0x40, 512);
    uint8_t *usb02mbr = (uint8_t*)aligned_alloc(0x40, 512);

    FSAEx_RawOpen(__wut_devoptab_fs_client, (char*)"/dev/usb01", &usb01Handle);
    FSAEx_RawRead(__wut_devoptab_fs_client, usb01mbr, 512, 1, 0, usb01Handle);

    FSAEx_RawOpen(__wut_devoptab_fs_client, (char*)"/dev/usb02", &usb02Handle);
    FSAEx_RawRead(__wut_devoptab_fs_client, usb02mbr, 512, 1, 0, usb02Handle);

    VPADStatus status;
    VPADReadError error;

    int cursorPos = 0;

    while(WHBProcIsRunning()) {
        VPADRead(VPAD_CHAN_0, &status, 1, &error);
        clearBuffers();

        printToScreen(0, cursorPos, ">");

        if(usb01mbr[511] == 0xAA)
            printToScreen(1, 0, "USB01: Unhidden (MBR: %X)", usb01mbr[511]);
        else if(usb01mbr[511] == 0xAB)
            printToScreen(1, 0, "USB01: Hidden (MBR: %X)", usb01mbr[511]);
        else
            printToScreen(1, 0, "USB01: Invalid MBR (MBR: %X)", usb01mbr[511]);

        if(usb02mbr[511] == 0xAA)
            printToScreen(1, 1, "USB02: Unhidden (MBR: %X)", usb02mbr[511]);
        else if(usb02mbr[511] == 0xAB)
            printToScreen(1, 1, "USB02: Hidden (MBR: %X)", usb02mbr[511]);
        else
            printToScreen(1, 1, "USB02: Invalid MBR (MBR: %X)", usb02mbr[511]);
        
        printToScreen(1, 3, "Press A to toggle USB %s", cursorPos == 0 ? "01" : "02");

        flipBuffers();
        if (status.trigger & VPAD_BUTTON_DOWN)
            cursorPos++;
        if (status.trigger & VPAD_BUTTON_UP)
            cursorPos--;
        if (cursorPos < 0)
            cursorPos = 0;
        if (cursorPos > 1)
            cursorPos = 1;
        if (status.trigger & VPAD_BUTTON_A) {
            switch (cursorPos) {
            case 0:
                hideOrUnhideUSB(usb01mbr, usb01Handle);
                break;
            case 1:
                hideOrUnhideUSB(usb02mbr, usb02Handle);
                break;
            default:
                break;
            }
        }
    }

    if (tvBuffer) free(tvBuffer);
    if (drcBuffer) free(drcBuffer);

    OSScreenShutdown();
    WHBProcShutdown();
    FSAEx_RawClose(__wut_devoptab_fs_client, usb01Handle);
    Mocha_DeInitLibrary();
    IOSUHAX_Close();
    return 0;
}
