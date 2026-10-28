#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspctrl.h>
#include <psppower.h>
#include <psprtc.h>
#include <stdio.h>

PSP_MODULE_INFO("GrannyPSP", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

#define BUF_WIDTH  512
#define SCR_WIDTH  480
#define SCR_HEIGHT 272
#define FRAME_SIZE (BUF_WIDTH * SCR_HEIGHT * 4)

static unsigned int __attribute__((aligned(16))) list[262144];
static int running = 1;

static int exit_callback(int arg1, int arg2, void *common) {
    running = 0;
    return 0;
}
static int callback_thread(SceSize args, void *argp) {
    int cbid = sceKernelCreateCallback("exit_cb", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int thid = sceKernelCreateThread("cb_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}

typedef struct {
    unsigned int color;
    float x, y, z;
} Vertex;

/* أرضية رمادية زرقاء */
static Vertex __attribute__((aligned(16))) floor_quad[6] = {
    {0xFF804020, -5.0f, 0.0f,  5.0f},
    {0xFF804020,  5.0f, 0.0f,  5.0f},
    {0xFF402010,  5.0f, 0.0f, -15.0f},
    {0xFF804020, -5.0f, 0.0f,  5.0f},
    {0xFF402010,  5.0f, 0.0f, -15.0f},
    {0xFF402010, -5.0f, 0.0f, -15.0f},
};

/* جدار في الخلف */
static Vertex __attribute__((aligned(16))) back_wall[6] = {
    {0xFF2040A0, -5.0f, 0.0f, -15.0f},
    {0xFF2040A0,  5.0f, 0.0f, -15.0f},
    {0xFF2040A0,  5.0f, 4.0f, -15.0f},
    {0xFF2040A0, -5.0f, 0.0f, -15.0f},
    {0xFF2040A0,  5.0f, 4.0f, -15.0f},
    {0xFF2040A0, -5.0f, 4.0f, -15.0f},
};

/* مكعب صغير أحمر نراه في الأمام */
static Vertex __attribute__((aligned(16))) box[6] = {
    {0xFF0000FF, -1.0f, 0.0f, -6.0f},
    {0xFF0000FF,  1.0f, 0.0f, -6.0f},
    {0xFF0000FF,  1.0f, 2.0f, -6.0f},
    {0xFF0000FF, -1.0f, 0.0f, -6.0f},
    {0xFF0000FF,  1.0f, 2.0f, -6.0f},
    {0xFF0000FF, -1.0f, 2.0f, -6.0f},
};

static void init_graphics(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void *)FRAME_SIZE, BUF_WIDTH);
    sceGuDepthBuffer((void *)(FRAME_SIZE * 2), BUF_WIDTH);
    sceGuOffset(2048 - (SCR_WIDTH / 2), 2048 - (SCR_HEIGHT / 2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

int main(void) {
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    init_graphics();

    SceCtrlData pad;

    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_START) break;

        sceGuStart(GU_DIRECT, list);

        sceGuClearColor(0xFF100808);
        sceGuClearDepth(0);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

        sceGumMatrixMode(GU_PROJECTION);
        sceGumLoadIdentity();
        sceGumPerspective(75.0f, 16.0f / 9.0f, 0.5f, 100.0f);

        sceGumMatrixMode(GU_VIEW);
        sceGumLoadIdentity();
        ScePspFVector3 eye = {0.0f, -1.7f, 0.0f};
        sceGumTranslate(&eye);

        sceGumMatrixMode(GU_MODEL);
        sceGumLoadIdentity();

        sceGuDisable(GU_TEXTURE_2D);
        sceGumDrawArray(GU_TRIANGLES,
                        GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        6, 0, floor_quad);
        sceGumDrawArray(GU_TRIANGLES,
                        GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        6, 0, back_wall);
        sceGumDrawArray(GU_TRIANGLES,
                        GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        6, 0, box);

        sceGuFinish();
        sceGuSync(0, 0);

        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
