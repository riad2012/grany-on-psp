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

/* ---------- زر HOME ---------- */
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

/* ---------- أرضية تجريبية (لنتأكد أن الـ3D يعمل) ---------- */
typedef struct {
    unsigned int color;
    float x, y, z;
} Vertex;

static Vertex __attribute__((aligned(16))) floor_quad[6] = {
    {0xFF1A2530, -10.0f, 0.0f,  2.0f},
    {0xFF1A2530,  10.0f, 0.0f,  2.0f},
    {0xFF0A1015,  10.0f, 0.0f, -20.0f},
    {0xFF1A2530, -10.0f, 0.0f,  2.0f},
    {0xFF0A1015,  10.0f, 0.0f, -20.0f},
    {0xFF0A1015, -10.0f, 0.0f, -20.0f},
};

/* ---------- تهيئة الرسوميات ---------- */
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

    /* 1) رفع تردد المعالج إلى 333MHz */
    scePowerSetClockFrequency(333, 333, 166);

    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    init_graphics();

    /* متغيرات عدّاد الفريمات */
    u64 last_tick, now_tick;
    u32 tick_res = sceRtcGetTickResolution();
    sceRtcGetCurrentTick(&last_tick);
    int frames = 0, fps = 0;
    int draw_offset = 0;   /* أي بافر نرسم عليه الآن */

    SceCtrlData pad;

    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_START) break;

        sceGuStart(GU_DIRECT, list);

        /* شاشة سوداء */
        sceGuClearColor(0xFF000000);
        sceGuClearDepth(0);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

        /* الكاميرا (منظور ثلاثي الأبعاد) */
        sceGumMatrixMode(GU_PROJECTION);
        sceGumLoadIdentity();
        sceGumPerspective(70.0f, 16.0f / 9.0f, 0.2f, 100.0f);

        sceGumMatrixMode(GU_VIEW);
        sceGumLoadIdentity();
        ScePspFVector3 eye = {0.0f, -1.6f, 0.0f};  /* ارتفاع عيني اللاعب */
        sceGumTranslate(&eye);

        sceGumMatrixMode(GU_MODEL);
        sceGumLoadIdentity();

        /* ارسم الأرضية */
        sceGumDrawArray(GU_TRIANGLES,
                        GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        6, 0, floor_quad);

        sceGuFinish();
        sceGuSync(0, 0);

        /* عدّاد الفريمات */
        frames++;
        sceRtcGetCurrentTick(&now_tick);
        if ((now_tick - last_tick) >= tick_res) {
            fps = frames;
            frames = 0;
            last_tick = now_tick;
        }

        /* اكتب النص على البافر الحالي */
        pspDebugScreenSetOffset(draw_offset);
        pspDebugScreenSetXY(0, 0);
        pspDebugScreenPrintf("FPS: %d   CPU: %dMHz   BUS: %dMHz   \n",
                             fps,
                             scePowerGetCpuClockFrequency(),
                             scePowerGetBusClockFrequency());
        pspDebugScreenPrintf("Free RAM: %d KB\n", (int)(sceKernelTotalFreeMemSize() / 1024));

        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
        draw_offset = draw_offset ? 0 : FRAME_SIZE;
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
