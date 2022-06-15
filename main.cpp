#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include <coreinit/screen.h>
#include <coreinit/cache.h>
#include <padscore/kpad.h>
#include <vpad/input.h>
#include <whb/proc.h>

#include <iosuhax.h>
#include <iosuhax_disc_interface.h>

size_t tvBufferSize;
size_t drcBufferSize;
void* tvBuffer;
void* drcBuffer;

int fsaFd;
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
        IOSUHAX_FSA_RawWrite(fsaFd, mbr, 512, 1, 0, handle);
    } else if(mbr[511] == 0xAB) {
        mbr[511]--;
        IOSUHAX_FSA_RawWrite(fsaFd, mbr, 512, 1, 0, handle);
    } else
        return;
}

int main() {
    WHBProcInit();

    OSScreenInit();

    VPADInit();
    WPADInit();
    KPADInit();
    WPADEnableURCC(1);

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
        printToScreen(0, 0, "IOSUHAX_FSA_Open failed");
        flipBuffers();
        WHBProcShutdown();
        return 1;
    }

    uint8_t *usb01mbr = (uint8_t*)aligned_alloc(0x40, 512);
    uint8_t *usb02mbr = (uint8_t*)aligned_alloc(0x40, 512);

    IOSUHAX_FSA_RawOpen(fsaFd, (char*)"/dev/usb01", &usb01Handle);
    IOSUHAX_FSA_RawRead(fsaFd, usb01mbr, 512, 1, 0, usb01Handle);

    IOSUHAX_FSA_RawOpen(fsaFd, (char*)"/dev/usb02", &usb02Handle);
    IOSUHAX_FSA_RawRead(fsaFd, usb02mbr, 512, 1, 0, usb02Handle);

    VPADStatus status;
    VPADReadError error;
    KPADStatus kpad_status;

    int cursorPos = 0;

    while(WHBProcIsRunning()) {
        VPADRead(VPAD_CHAN_0, &status, 1, &error);
        memset(&kpad_status, 0, sizeof(KPADStatus));
        WPADExtensionType controllerType;
        for (int i = 0; i < 4; i++) {
            if (WPADProbe((WPADChan) i, &controllerType) == 0) {
                KPADRead((WPADChan) i, &kpad_status, 1);
                break;
            }
        }

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
        if ((status.trigger & (VPAD_BUTTON_DOWN | VPAD_STICK_L_EMULATION_DOWN)) |
            (kpad_status.trigger & (WPAD_BUTTON_DOWN)) |
            (kpad_status.classic.trigger & (WPAD_CLASSIC_BUTTON_DOWN | WPAD_CLASSIC_STICK_L_EMULATION_DOWN)) |
            (kpad_status.pro.trigger & (WPAD_PRO_BUTTON_DOWN | WPAD_PRO_STICK_L_EMULATION_DOWN)))
            cursorPos++;
        if ((status.trigger & (VPAD_BUTTON_UP | VPAD_STICK_L_EMULATION_UP)) |
            (kpad_status.trigger & (WPAD_BUTTON_UP)) |
            (kpad_status.classic.trigger & (WPAD_CLASSIC_BUTTON_UP | WPAD_CLASSIC_STICK_L_EMULATION_UP)) |
            (kpad_status.pro.trigger & (WPAD_PRO_BUTTON_UP | WPAD_PRO_STICK_L_EMULATION_UP)))
            cursorPos--;
        if (cursorPos < 0)
            cursorPos = 0;
        if (cursorPos > 1)
            cursorPos = 1;
        if ((status.trigger & VPAD_BUTTON_A) |
            ((kpad_status.trigger & (WPAD_BUTTON_A)) | (kpad_status.classic.trigger & (WPAD_CLASSIC_BUTTON_A)) |
            (kpad_status.pro.trigger & (WPAD_PRO_BUTTON_A)))) {
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
    IOSUHAX_FSA_RawClose(fsaFd, usb01Handle);
    IOSUHAX_FSA_RawClose(fsaFd, usb02Handle);
    IOSUHAX_Close();
    IOSUHAX_FSA_Close(fsaFd);
    return 0;
}
