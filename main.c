#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspctrl.h>
#include <psppower.h>
#include <psprtc.h>
#include <psputils.h>
#include <math.h>
#include <string.h>

PSP_MODULE_INFO("GrannyPSP", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

#define PI 3.14159265f
#define BUF_WIDTH  512
#define SCR_WIDTH  480
#define SCR_HEIGHT 272
#define FRAME_SIZE (BUF_WIDTH * SCR_HEIGHT * 4)

static unsigned int __attribute__((aligned(16))) list[262144];
static int running = 1;

static int exit_callback(int a1, int a2, void *c) { running = 0; return 0; }
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

/* ================= الهندسة ================= */
typedef struct { unsigned int color; float x, y, z; } Vertex;

#define MAX_WORLD 30000
#define MAX_GR 600
static Vertex __attribute__((aligned(16))) world[MAX_WORLD];
static Vertex __attribute__((aligned(16))) granny_v[MAX_GR];
static int world_n = 0, granny_n = 0;

static unsigned int shade(unsigned int c, float f) {
    int r = (int)((c & 0xFF) * f);
    int g = (int)(((c >> 8) & 0xFF) * f);
    int b = (int)(((c >> 16) & 0xFF) * f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return 0xFF000000u | ((unsigned)b << 16) | ((unsigned)g << 8) | (unsigned)r;
}

static void setv(Vertex *v, unsigned c, float x, float y, float z) {
    v->color = c; v->x = x; v->y = y; v->z = z;
}

static void quad(Vertex *a, int *n, int mx, unsigned c,
                 float x0, float y0, float z0, float x1, float y1, float z1,
                 float x2, float y2, float z2, float x3, float y3, float z3) {
    if (*n + 6 > mx) return;
    Vertex *v = &a[*n];
    setv(&v[0], c, x0, y0, z0);
    setv(&v[1], c, x1, y1, z1);
    setv(&v[2], c, x2, y2, z2);
    setv(&v[3], c, x0, y0, z0);
    setv(&v[4], c, x2, y2, z2);
    setv(&v[5], c, x3, y3, z3);
    *n += 6;
}

/* وجه مقسّم إلى خلايا صغيرة (يمنع مشاكل القطع قرب الكاميرا) */
static void face(Vertex *a, int *n, int mx, unsigned col, float f,
                 float ox, float oy, float oz,
                 float ux, float uy, float uz,
                 float vx, float vy, float vz, int nu, int nv) {
    for (int i = 0; i < nu; i++) {
        for (int j = 0; j < nv; j++) {
            float u0 = (float)i / nu, u1 = (float)(i + 1) / nu;
            float v0 = (float)j / nv, v1 = (float)(j + 1) / nv;
            unsigned c = shade(col, f * (((i + j) & 1) ? 0.88f : 1.0f));
            quad(a, n, mx, c,
                 ox + ux*u0 + vx*v0, oy + uy*u0 + vy*v0, oz + uz*u0 + vz*v0,
                 ox + ux*u1 + vx*v0, oy + uy*u1 + vy*v0, oz + uz*u1 + vz*v0,
                 ox + ux*u1 + vx*v1, oy + uy*u1 + vy*v1, oz + uz*u1 + vz*v1,
                 ox + ux*u0 + vx*v1, oy + uy*u0 + vy*v1, oz + uz*u0 + vz*v1);
        }
    }
}

static void box_sub(Vertex *a, int *n, int mx,
                    float x0, float y0, float z0, float x1, float y1, float z1,
                    unsigned col, float cell, int top) {
    float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    int nx = (int)(dx / cell) + 1, ny = (int)(dy / cell) + 1, nz = (int)(dz / cell) + 1;
    face(a, n, mx, col, 0.90f, x0, y0, z0, dx, 0, 0, 0, dy, 0, nx, ny);
    face(a, n, mx, col, 0.75f, x0, y0, z1, dx, 0, 0, 0, dy, 0, nx, ny);
    face(a, n, mx, col, 0.65f, x0, y0, z0, 0, 0, dz, 0, dy, 0, nz, ny);
    face(a, n, mx, col, 0.80f, x1, y0, z0, 0, 0, dz, 0, dy, 0, nz, ny);
    if (top) face(a, n, mx, col, 1.10f, x0, y1, z0, dx, 0, 0, 0, 0, dz, nx, nz);
}

/* ================= الاصطدام ================= */
typedef struct { float x0, z0, x1, z1; } Rect;
static Rect rects[40];
static int nrect = 0;

static void add_solid(float x0, float z0, float x1, float z1, float h,
                      unsigned col, int top) {
    box_sub(world, &world_n, MAX_WORLD, x0, 0, z0, x1, h, z1, col, 1.0f, top);
    rects[nrect].x0 = x0; rects[nrect].z0 = z0;
    rects[nrect].x1 = x1; rects[nrect].z1 = z1;
    nrect++;
}

static int blocked(float x, float z, float r) {
    for (int i = 0; i < nrect; i++) {
        if (x > rects[i].x0 - r && x < rects[i].x1 + r &&
            z > rects[i].z0 - r && z < rects[i].z1 + r) return 1;
    }
    return 0;
}

static void move_to(float *px, float *pz, float dx, float dz, float r) {
    if (!blocked(*px + dx, *pz, r)) *px += dx;
    if (!blocked(*px, *pz + dz, r)) *pz += dz;
}

static int los(float ax, float az, float bx, float bz) {
    float dx = bx - ax, dz = bz - az;
    float d = sqrtf(dx * dx + dz * dz);
    int steps = (int)(d / 0.25f) + 1;
    for (int i = 1; i < steps; i++) {
        float t = (float)i / steps;
        if (blocked(ax + dx * t, az + dz * t, 0.0f)) return 0;
    }
    return 1;
}

/* ================= بناء البيت ================= */
static void build_world(void) {
    unsigned WALL = 0xFF4A665A;   /* ورق جدران قديم */
    unsigned FLOOR = 0xFF1E2C3C;  /* خشب */
    unsigned CEIL = 0xFF282828;
    unsigned WOOD = 0xFF18283A;

    /* أرضية وسقف: البيت 18 x 8 متر */
    face(world, &world_n, MAX_WORLD, FLOOR, 1.0f, 0, 0, 0, 18, 0, 0, 0, 0, 8, 18, 8);
    face(world, &world_n, MAX_WORLD, CEIL, 1.0f, 0, 3, 0, 18, 0, 0, 0, 0, 8, 18, 8);

    /* الجدران الخارجية */
    add_solid(-0.3f, -0.3f, 18.3f, 0.0f, 3.0f, WALL, 0);
    add_solid(-0.3f, 8.0f, 18.3f, 8.3f, 3.0f, WALL, 0);
    add_solid(-0.3f, 0.0f, 0.0f, 8.0f, 3.0f, WALL, 0);
    add_solid(18.0f, 0.0f, 18.3f, 8.0f, 3.0f, WALL, 0);

    /* الجدران الفاصلة (فيها أبواب عند z بين 3.2 و 4.8) */
    add_solid(5.85f, 0.0f, 6.15f, 3.2f, 3.0f, WALL, 0);
    add_solid(5.85f, 4.8f, 6.15f, 8.0f, 3.0f, WALL, 0);
    add_solid(11.85f, 0.0f, 12.15f, 3.2f, 3.0f, WALL, 0);
    add_solid(11.85f, 4.8f, 12.15f, 8.0f, 3.0f, WALL, 0);

    /* غرفة 1 (غرفة نوم): سرير وطاولة جانبية */
    add_solid(0.5f, 5.8f, 2.5f, 7.8f, 0.6f, 0xFF2020A0, 1);
    add_solid(3.0f, 7.2f, 3.8f, 7.8f, 0.5f, WOOD, 1);
    /* غرفة 2 (مطبخ): طاولة وصناديق */
    add_solid(8.0f, 0.8f, 10.0f, 1.8f, 0.8f, WOOD, 1);
    add_solid(7.0f, 6.5f, 8.0f, 7.5f, 1.0f, 0xFF204060, 1);
    /* غرفة 3 (مخزن): خزانة ورف */
    add_solid(13.0f, 0.2f, 15.5f, 1.2f, 2.2f, WOOD, 1);
    add_solid(16.5f, 6.8f, 17.8f, 7.8f, 1.2f, WOOD, 1);
}

static void build_granny(void) {
    unsigned DRESS = 0xFF6A4A5A, SKIN = 0xFF9AB4D0, HAIR = 0xFFC0C0C0, EYE = 0xFF0000AA;
    int *n = &granny_n;
    box_sub(granny_v, n, MAX_GR, -0.30f, 0.0f, -0.20f, 0.30f, 1.20f, 0.20f, DRESS, 10.0f, 1);
    box_sub(granny_v, n, MAX_GR, -0.17f, 1.20f, -0.17f, 0.17f, 1.50f, 0.17f, SKIN, 10.0f, 1);
    box_sub(granny_v, n, MAX_GR, -0.19f, 1.50f, -0.19f, 0.19f, 1.62f, 0.19f, HAIR, 10.0f, 1);
    box_sub(granny_v, n, MAX_GR, -0.11f, 1.34f, -0.19f, -0.04f, 1.40f, -0.17f, EYE, 10.0f, 1);
    box_sub(granny_v, n, MAX_GR, 0.04f, 1.34f, -0.19f, 0.11f, 1.40f, -0.17f, EYE, 10.0f, 1);
    box_sub(granny_v, n, MAX_GR, -0.38f, 0.90f, -0.60f, -0.30f, 1.05f, -0.10f, DRESS, 10.0f, 1);
    box_sub(granny_v, n, MAX_GR, 0.30f, 0.90f, -0.60f, 0.38f, 1.05f, -0.10f, DRESS, 10.0f, 1);
}

/* ================= الرسوميات ================= */
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
    sceGuEnable(GU_FOG);
    sceGuShadeModel(GU_SMOOTH);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

/* ================= ذكاء الجدة ================= */
static int room_of(float x) {
    int r = (int)(x / 6.0f);
    if (r < 0) r = 0;
    if (r > 2) r = 2;
    return r;
}

/* تحدد النقطة التالية: تمرّ عبر الأبواب بشكل صحيح */
static void nav(float fx, float fz, float tx, float tz, float *ox, float *oz) {
    int a = room_of(fx), b = room_of(tx);
    if (a == b) { *ox = tx; *oz = tz; return; }
    int dir = (b > a) ? 1 : -1;
    float doorx = 6.0f * (dir > 0 ? a + 1 : a);
    float ax = doorx - dir * 1.0f;
    float bx = doorx + dir * 1.0f;
    if (fabsf(fz - 4.0f) < 0.4f && fabsf(fx - doorx) < 1.3f) { *ox = bx; *oz = 4.0f; }
    else { *ox = ax; *oz = 4.0f; }
}

static const float wps[6][2] = {
    {15.0f, 5.5f}, {9.0f, 6.0f}, {3.0f, 5.5f},
    {3.0f, 2.0f}, {9.0f, 2.9f}, {15.0f, 2.9f}
};

int main(void) {
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);   /* 333MHz */
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    build_world();
    build_granny();
    sceKernelDcacheWritebackAll();
    init_graphics();

    float px = 3.0f, pz = 4.0f, yaw = -PI / 2;
    float gx = 15.0f, gz = 4.0f, gyaw = PI / 2;
    int state = 0, wp = 0;
    float lost = 0.0f, lkx = 0.0f, lkz = 0.0f, caught = 0.0f;

    u64 last, now;
    u32 res = sceRtcGetTickResolution();
    sceRtcGetCurrentTick(&last);
    SceCtrlData pad;

    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_START) break;

        sceRtcGetCurrentTick(&now);
        float dt = (float)(now - last) / (float)res;
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.0f) dt = 0.0f;

        int run_f = 0;

        if (caught > 0.0f) {
            caught -= dt;
        } else {
            /* ---- تحكم اللاعب ---- */
            float ax = ((int)pad.Lx - 128) / 128.0f;
            float ay = ((int)pad.Ly - 128) / 128.0f;
            if (fabsf(ax) < 0.2f) ax = 0.0f;
            if (fabsf(ay) < 0.2f) ay = 0.0f;
            float fwd = -ay, turn = -ax, str = 0.0f;
            if (pad.Buttons & PSP_CTRL_UP) fwd = 1.0f;
            if (pad.Buttons & PSP_CTRL_DOWN) fwd = -1.0f;
            if (pad.Buttons & PSP_CTRL_LEFT) turn = 1.0f;
            if (pad.Buttons & PSP_CTRL_RIGHT) turn = -1.0f;
            if (pad.Buttons & PSP_CTRL_LTRIGGER) str = -1.0f;
            if (pad.Buttons & PSP_CTRL_RTRIGGER) str = 1.0f;
            run_f = ((pad.Buttons & PSP_CTRL_CROSS) && (fwd != 0.0f || str != 0.0f));
            float spd = run_f ? 4.0f : 2.2f;

            yaw += turn * 2.0f * dt;
            float s = sinf(yaw), c = cosf(yaw);
            float mx = (-s * fwd + c * str) * spd * dt;
            float mz = (-c * fwd - s * str) * spd * dt;
            move_to(&px, &pz, mx, mz, 0.3f);

            /* ---- ذكاء الجدة ---- */
            float ddx = px - gx, ddz = pz - gz;
            float d = sqrtf(ddx * ddx + ddz * ddz);
            int sees = (d < 9.0f) && los(gx, gz, px, pz);
            int hears = (run_f && d < 7.0f) || (d < 2.0f);
            if (sees || hears) {
                state = 1; lost = 0.0f; lkx = px; lkz = pz;
            } else if (state == 1) {
                lost += dt;
                if (lost > 5.0f) state = 0;
            }

            float tx, tz;
            if (state == 0) {
                tx = wps[wp][0]; tz = wps[wp][1];
                float wx = tx - gx, wz = tz - gz;
                if (sqrtf(wx * wx + wz * wz) < 0.6f) wp = (wp + 1) % 6;
            } else { tx = lkx; tz = lkz; }

            float nx, nz;
            nav(gx, gz, tx, tz, &nx, &nz);
            float dx = nx - gx, dz = nz - gz;
            float dl = sqrtf(dx * dx + dz * dz);
            if (dl > 0.05f) {
                dx /= dl; dz /= dl;
                float gs = (state == 1) ? 2.6f : 1.2f;
                move_to(&gx, &gz, dx * gs * dt, dz * gs * dt, 0.35f);
                gyaw = atan2f(-dx, -dz);
            }

            /* ---- الإمساك باللاعب ---- */
            if (d < 0.9f) {
                caught = 1.2f;
                px = 3.0f; pz = 4.0f; yaw = -PI / 2;
                gx = 15.0f; gz = 4.0f; state = 0; wp = 0;
            }
        }

        /* ---- الرسم ---- */
        unsigned int bg = (caught > 0.0f) ? 0xFF000070 : 0xFF080808;
        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(bg);
        sceGuClearDepth(0);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
        sceGuFog(0.5f, 10.0f, bg);

        sceGumMatrixMode(GU_PROJECTION);
        sceGumLoadIdentity();
        sceGumPerspective(75.0f, 16.0f / 9.0f, 0.1f, 60.0f);

        /* مصفوفة الكاميرا (عين اللاعب على ارتفاع 1.6 متر) */
        float cs = cosf(yaw), sn = sinf(yaw);
        float ex = px, ey = 1.6f, ez = pz;
        ScePspFMatrix4 view;
        view.x.x = cs;   view.x.y = 0; view.x.z = sn;   view.x.w = 0;
        view.y.x = 0;    view.y.y = 1; view.y.z = 0;    view.y.w = 0;
        view.z.x = -sn;  view.z.y = 0; view.z.z = cs;   view.z.w = 0;
        view.w.x = -(cs * ex - sn * ez);
        view.w.y = -ey;
        view.w.z = -(sn * ex + cs * ez);
        view.w.w = 1.0f;
        sceGumMatrixMode(GU_VIEW);
        sceGumLoadMatrix(&view);

        sceGumMatrixMode(GU_MODEL);
        sceGumLoadIdentity();
        sceGumDrawArray(GU_TRIANGLES,
                        GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        world_n, 0, world);

        /* الجدة */
        float gc = cosf(gyaw), gs2 = sinf(gyaw);
        ScePspFMatrix4 mm;
        mm.x.x = gc;   mm.x.y = 0; mm.x.z = -gs2; mm.x.w = 0;
        mm.y.x = 0;    mm.y.y = 1; mm.y.z = 0;    mm.y.w = 0;
        mm.z.x = gs2;  mm.z.y = 0; mm.z.z = gc;   mm.z.w = 0;
        mm.w.x = gx;   mm.w.y = 0; mm.w.z = gz;   mm.w.w = 1.0f;
        sceGumMatrixMode(GU_MODEL);
        sceGumLoadMatrix(&mm);
        sceGumDrawArray(GU_TRIANGLES,
                        GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        granny_n, 0, granny_v);

        sceGuFinish();
        sceGuSync(0, 0);
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
