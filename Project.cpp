/* Corridor of the Dead  "Haunted Night" - GLUT/OpenGL
   One university floor (corridor, 8 classrooms, washroom with 3 cubicles,
   2 elevators) with zombies to shoot; ride Elevator A up. Clear 3 floors to win.
   Old-school ghost look: fog, flickering tubes, candles, cobwebs, scrawled
   walls, dust and a grainy film overlay.
   Controls: WASD/Arrows move, Shift run, mouse look,
             Left Click (hold) or Space shoot,
             E or Right Click open/close the nearest door or use the lift,
             O use the lift, R restart after dying/winning, ESC quit.
   Build Linux: g++ main.cpp -o corridor -lglut -lGLU -lGL -lm
*/

#define _CRT_SECURE_NO_WARNINGS

#include <GL/glut.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <vector>

#define WIN_W 1100
#define WIN_H 720
#define PI 3.14159265358979323846f
#define MAX_DOORS 64
#define TEXSZ 256
#define LIFT_DOOR_INDEX 12 /* "Elevator A" is always the 13th door added */

static float camX = 0.0f, camY = 1.7f, camZ = 10.0f;
static float yaw = -90.0f, pitch = 0.0f;
static int keyState[256], specialState[256];
static int warpingPointer = 0, mouseFire = 0;
static const float PLAYER_R = 0.34f;

/* ---- combat / gun state ---- */
struct Tracer { float x1, y1, z1, x2, y2, z2, age; };
static std::vector<Tracer> tracers;
static const float TRACER_LIFE = 0.08f;
static int   playerHealth = 100, killCount = 0, shotsFired = 0, lastShotMs = -10000;
static bool  gameOver = false, gameWon = false;
static float muzzleFlashTimer = 0.0f, gunBobPhase = 0.0f, fanAngle = 0.0f;
static float hauntTime = 0.0f; /* drives every flicker, drift and bob */

/* ---- multi-floor lift progression ----
   Floors need 3/4/5 kills. liftState: 0 = locked (quota not met),
   2 = open (quota met, walk in), 3 = closed with the shooter inside
   (next use rides to the next floor). The second elevator is decorative. */
static const int FLOOR_KILL_REQUIREMENT[3] = { 4,5,6 };
static int   currentFloor = 1, liftState = 0;
static float liftDoorOpen = 0.0f;  /* 0..1 visual slide-open amount */
static float floorMsgTimer = 0.0f; /* brief "Floor N" banner after a ride */

typedef struct {
    float x, y, z;
    char label[48];
    int type;       /* 0 classroom, 1 elevator, 2 washroom, 3 washroom cubicle */
    float angle;
} Door;

static Door doors[MAX_DOORS];
static int doorCount = 0, nearestDoor = -1;
static int doorTarget[MAX_DOORS];

typedef struct { float minx, maxx, minz, maxz; } Rect;
static Rect walls[128], obstacles[256];
static int wallCount = 0, obstacleCount = 0;

static GLubyte texBuf[TEXSZ * TEXSZ * 3];
static GLuint tileTex, woodTex, metalTex, wallTex, sceneTex;

static const float ROOM_X[4] = { -16,-6,6,16 };

/* ------------------------------------------------------------ */
/* Collision                                                    */
/* ------------------------------------------------------------ */

static void addRect(Rect* list, int* n, int cap, float minx, float maxx, float minz, float maxz) {
    if (*n >= cap)return;
    Rect r = { minx,maxx,minz,maxz };
    list[(*n)++] = r;
}
static void addWall(float a, float b, float c, float d) { addRect(walls, &wallCount, 128, a, b, c, d); }
static void addObstacle(float a, float b, float c, float d) { addRect(obstacles, &obstacleCount, 256, a, b, c, d); }

static int pointInRect(float x, float z, float minx, float maxx, float minz, float maxz) {
    return x > minx && x < maxx && z > minz && z < maxz;
}

/* Circle of radius r vs. axis-aligned rectangle. */
static int rectHit(float x, float z, float r, float minx, float maxx, float minz, float maxz) {
    return x + r > minx && x - r < maxx && z + r > minz && z - r < maxz;
}

static int hitRects(const Rect* list, int n, float x, float z) {
    for (int i = 0; i < n; i++)
        if (rectHit(x, z, PLAYER_R, list[i].minx, list[i].maxx, list[i].minz, list[i].maxz)) return 1;
    return 0;
}
static int collidesStatic(float x, float z) { return hitRects(walls, wallCount, x, z); }
static int collidesFurniture(float x, float z) { return hitRects(obstacles, obstacleCount, x, z); }

/* Door collision follows the actual hinged leaf: a closed door seals the
   doorway, an open door blocks where it has swung to. Classroom/washroom
   leaves hinge at (-1.08, .03) and are 2.16m long; cubicle leaves hinge at
   -1.02 and are 2.04m long. */
static int collidesDoors(float x, float z) {
    for (int i = 0; i < doorCount; i++) {
        const Door* d = &doors[i];

        if (d->type == 1) {
            /* The story lift stops blocking only while its doors are open. */
            if (!(i == LIFT_DOOR_INDEX && liftState == 2) &&
                pointInRect(x, z, d->x - 1.18f, d->x + 1.18f, d->z - .34f, d->z + .34f))
                return 1;
            continue;
        }

        int cub = d->type == 3;
        float hx = d->x - (cub ? 1.02f : 1.08f), hz = d->z + (cub ? 0.0f : .03f);
        float len = cub ? 2.04f : 2.16f, halfW = (cub ? .04f : .055f) + PLAYER_R;
        float seal = cub ? .10f : .34f;
        float a = d->angle * PI / 180.0f, ca = cosf(a), sa = sinf(a);
        float px = x - hx, pz = z - hz;
        float along = px * ca - pz * sa, across = px * sa + pz * ca;

        if (along > -PLAYER_R && along < len + PLAYER_R && fabsf(across) < halfW)
            return 1;
        if (fabsf(d->angle) < 8.0f &&
            pointInRect(x, z, d->x - 1.02f, d->x + 1.02f, d->z - seal, d->z + seal))
            return 1;
    }
    return 0;
}

static int collides(float x, float z) {
    return collidesStatic(x, z) || collidesDoors(x, z) || collidesFurniture(x, z);
}

static void addDoor(float x, float y, float z, const char* label, int type) {
    if (doorCount >= MAX_DOORS)return;
    Door* d = &doors[doorCount++];
    d->x = x; d->y = y; d->z = z; d->type = type; d->angle = 0;
    strncpy(d->label, label, 47); d->label[47] = '\0';
}

/* ------------------------------------------------------------ */
/* Procedural textures                                          */
/* ------------------------------------------------------------ */

static float clampf(float v, float lo, float hi) { return fmaxf(lo, fminf(hi, v)); }
static float frand(void) { return (float)rand() / (float)RAND_MAX; }

static void setPx(int x, int y, float r, float g, float b) {
    if (x < 0 || x >= TEXSZ || y < 0 || y >= TEXSZ)return;
    GLubyte* p = texBuf + (y * TEXSZ + x) * 3;
    p[0] = (GLubyte)clampf(r, 0, 255); p[1] = (GLubyte)clampf(g, 0, 255); p[2] = (GLubyte)clampf(b, 0, 255);
}

/* Blend a pixel toward (r,g,b) by a (0..1). */
static void mixPx(int x, int y, float r, float g, float b, float a) {
    if (x < 0 || x >= TEXSZ || y < 0 || y >= TEXSZ)return;
    GLubyte* p = texBuf + (y * TEXSZ + x) * 3;
    setPx(x, y, p[0] + (r - p[0]) * a, p[1] + (g - p[1]) * a, p[2] + (b - p[2]) * a);
}

static void fillRect(int x0, int x1, int y0, int y1, int r, int g, int b) {
    for (int y = y0; y < y1; y++)for (int x = x0; x < x1; x++)setPx(x, y, r, g, b);
}

/* Thick line of discs, used for tree branches and grave crosses. */
static void texLine(float x0, float y0, float x1, float y1, float th, int r, int g, int b) {
    int n = (int)(hypotf(x1 - x0, y1 - y0) * 2.0f) + 1;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n, cx = x0 + (x1 - x0) * t, cy = y0 + (y1 - y0) * t;
        for (int oy = (int)-th; oy <= (int)th; oy++)for (int ox = (int)-th; ox <= (int)th; ox++)
            if (ox * ox + oy * oy <= th * th + .5f) setPx((int)cx + ox, (int)cy + oy, r, g, b);
    }
}

/* Integer hash -> 0..1. */
static float hash2(int x, int y) {
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}

/* Smooth value noise with `cells` lattice cells per texture edge; wraps at
   TEXSZ so the textures still tile seamlessly. */
static float vnoise(int x, int y, int cells) {
    float fx = (float)x * cells / TEXSZ, fy = (float)y * cells / TEXSZ;
    int x0 = (int)fx, y0 = (int)fy;
    float tx = fx - x0, ty = fy - y0;
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
    int x1 = (x0 + 1) % cells, y1 = (y0 + 1) % cells;
    x0 %= cells; y0 %= cells;
    float a = hash2(x0, y0), b = hash2(x1, y0), c = hash2(x0, y1), d = hash2(x1, y1);
    return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty;
}
static float fbm(int x, int y) { return .5f * vnoise(x, y, 4) + .3f * vnoise(x, y, 8) + .2f * vnoise(x, y, 32); }

static void uploadTex(GLuint* id) {
    glGenTextures(1, id);
    glBindTexture(GL_TEXTURE_2D, *id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); /* crunchy retro texels */
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, TEXSZ, TEXSZ, GL_RGB, GL_UNSIGNED_BYTE, texBuf);
}

/* Silhouette of a leafless tree, grown recursively (y points down). */
static void texBranch(float x, float y, float len, float ang, int depth) {
    float x1 = x + cosf(ang) * len, y1 = y - sinf(ang) * len;
    texLine(x, y, x1, y1, depth * .45f + .3f, 6, 7, 10);
    if (depth <= 0)return;
    texBranch(x1, y1, len * (.62f + frand() * .15f), ang + .35f + frand() * .35f, depth - 1);
    texBranch(x1, y1, len * (.58f + frand() * .15f), ang - .35f - frand() * .35f, depth - 1);
}

static void genTextures(void) {
    int x, y, i;
    srand(1337); /* the grime and cracks come out the same every run */

    /* Floor: faded old-school checker tiles, grimy, stained and cracked. */
    for (y = 0; y < TEXSZ; y++)for (x = 0; x < TEXSZ; x++) {
        float r, g, b;
        if (x % 64 < 2 || y % 64 < 2) { r = 34; g = 33; b = 29; }
        else if (((x / 64) + (y / 64)) & 1) { r = 150; g = 146; b = 126; }
        else { r = 46; g = 48; b = 45; }
        float k = .50f + .60f * fbm(x, y) + (hash2(x, y) - .5f) * .10f;
        r *= k; g *= k; b *= k;
        if (vnoise(x, y, 4) > .66f) { r *= .72f; g *= .62f; b *= .48f; } /* old brown stains */
        setPx(x, y, r, g, b);
    }
    for (i = 0; i < 10; i++) {
        float cx = (float)(rand() % TEXSZ), cy = (float)(rand() % TEXSZ), ang = frand() * 2 * PI;
        for (int st = 0; st < 70; st++) {
            ang += (frand() - .5f) * .9f;
            cx += cosf(ang); cy += sinf(ang);
            setPx((int)cx & (TEXSZ - 1), (int)cy & (TEXSZ - 1), 18, 18, 16);
        }
    }
    uploadTex(&tileTex);

    /* Weathered dark wood: the old grain and seams, blackened and blotchy. */
    for (y = 0; y < TEXSZ; y++)for (x = 0; x < TEXSZ; x++) {
        float g = 10 * sinf(y * .16f + x * .025f) + 4 * sinf(y * .47f);
        int seam = (x % 26 < 2) ? -34 : 0;
        float k = .42f + .38f * fbm(x, y);
        float r = (150 + g + seam) * k, gg = (104 + g * .55f + seam * .7f) * k, b = (62 + g * .3f + seam * .4f) * k;
        float avg = (r + gg + b) / 3;
        setPx(x, y, r + (avg - r) * .3f, gg + (avg - gg) * .3f, b + (avg - b) * .3f);
    }
    uploadTex(&woodTex);

    /* Tarnished metal eaten by rust. */
    for (y = 0; y < TEXSZ; y++)for (x = 0; x < TEXSZ; x++) {
        float c = 74 + 12 * sinf(x * .22f) + 6 * sinf(y * .08f);
        float rust = clampf((fbm(x, y) - .48f) * 3.0f, 0, 1), h = .7f + .3f * hash2(x, y);
        setPx(x, y, c + (108 * h - c) * rust, c + (52 * h - c) * rust, c + (24 * h - c) * rust);
    }
    uploadTex(&metalTex);

    /* Walls: faded striped damask wallpaper with water stains, streaks,
       mould and patches peeled back to bare plaster. */
    for (y = 0; y < TEXSZ; y++)for (x = 0; x < TEXSZ; x++) {
        float r = 70, g = 80, b = 68;
        int sx = x % 32;
        if (sx >= 11 && sx < 21) { r = 86; g = 94; b = 78; }
        if (sx == 10 || sx == 21) { r = 50; g = 56; b = 46; }
        float dx = (float)(sx < 16 ? sx : 32 - sx), dy = fabsf((float)(y % 64) - 32.0f);
        float dm = dx / 7.0f + dy / 14.0f;
        if ((dm < 1.0f && dm > .70f) || dm < .25f) { r = 112; g = 104; b = 76; }

        float shade = clampf(.50f + .65f * (vnoise(x, y, 4) * .6f + vnoise(x, y, 16) * .4f), .42f, 1.1f);
        if (vnoise(x, 0, 64) > .68f) shade *= .72f;
        r *= shade; g *= shade; b *= shade;
        if (vnoise(x, y, 32) > .76f) { r = r * .4f + 10; g = g * .4f + 16; b = b * .4f + 8; }

        float n = fbm(x, y);
        if (n > .71f) { r = 124; g = 115; b = 96; }
        else if (n > .68f) { r = 38; g = 34; b = 28; }
        setPx(x, y, r, g, b);
    }
    uploadTex(&wallTex);

    /* Night view for the window: stars, a haloed moon, black hills, dead
       trees, a graveyard and a band of ground mist. */
    for (y = 0; y < TEXSZ; y++)for (x = 0; x < TEXSZ; x++) {
        if (y < 150) {
            float t = (float)y / 150.0f;
            float r = 8 + 24 * t, g = 10 + 28 * t, b = 24 + 30 * t;
            float d = hypotf((float)(x - 186), (float)(y - 46));
            if (d < 58) { float h = 1 - d / 58; h *= h * 48; r += h; g += h; b += h * .9f; }
            if (d < 20) { float m = 208 + (vnoise(x, y, 32) - .5f) * 60; r = m; g = m; b = m * .88f; }
            else if (y < 125 && hash2(x, y) > .9965f) { float st = 150 + hash2(y, x) * 100; r = g = st; b = st + 10; }
            setPx(x, y, r, g, b);
        }
        else {
            float n = 5 * vnoise(x, y, 32);
            setPx(x, y, 14 + n, 20 + n, 17 + n);
        }
    }
    for (x = 0; x < TEXSZ; x++) {
        int top = (int)(136 - 9 * sinf(x * 2 * PI / TEXSZ * 2 + 1) - 5 * sinf(x * 2 * PI / TEXSZ * 5));
        for (y = top; y < 150; y++) setPx(x, y, 11, 14, 17);
    }
    for (y = 150; y < TEXSZ; y++) {
        float t = (float)(y - 150) / (float)(TEXSZ - 150), halfw = 6.0f + t * 55.0f;
        for (x = (int)(128 - halfw); x <= (int)(128 + halfw); x++) {
            float n = ((x * 3 + y * 5) % 9) - 4;
            setPx(x, y, 40 + n, 36 + n, 31 + n);
        }
    }
    {
        static const int graveX[7] = { 30,62,88,172,198,226,246 }, graveY[7] = { 196,178,222,186,230,170,205 };
        for (i = 0; i < 7; i++) {
            int gx = graveX[i], gy = graveY[i], w = 5 + (gy - 150) / 12, h = w * 2;
            if (i % 3 == 1) { /* cross */
                texLine((float)gx, (float)gy, (float)gx, (float)(gy - h - 4), w * .35f, 52, 54, 58);
                texLine((float)(gx - w), (float)(gy - h + 2), (float)(gx + w), (float)(gy - h + 2), w * .3f, 52, 54, 58);
                continue;
            }
            fillRect(gx - w, gx + w, gy - h + w, gy, 56, 58, 62);
            for (y = gy - h; y < gy - h + w; y++)for (x = gx - w; x < gx + w; x++)
                if (hypotf((float)(x - gx), (float)(y - (gy - h + w))) < w) setPx(x, y, 56, 58, 62);
            fillRect(gx - w + 2, gx + w - 2, gy - h + w + 2, gy - h + w + 3, 30, 31, 34); /* inscription */
        }
    }
    texBranch(22, 175, 24, PI / 2, 5);
    texBranch(236, 168, 28, PI / 2 + .1f, 5);
    texBranch(140, 150, 12, PI / 2 - .1f, 4);
    for (y = 125; y < 200; y++)for (x = 0; x < TEXSZ; x++) {
        float a = 1.0f - fabsf((float)y - 158.0f) / 42.0f;
        if (a > 0) mixPx(x, y, 72, 80, 86, a * .55f * (.55f + .45f * vnoise(x, y, 8)));
    }
    uploadTex(&sceneTex);
}

/* ------------------------------------------------------------ */
/* Drawing helpers                                               */
/* ------------------------------------------------------------ */

static void setColor(float r, float g, float b) {
    GLfloat a[] = { r * .35f,g * .35f,b * .35f,1 }, d[] = { r,g,b,1 };
    GLfloat s[] = { .18f,.18f,.18f,1 };
    glMaterialfv(GL_FRONT, GL_AMBIENT, a);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, d);
    glMaterialfv(GL_FRONT, GL_SPECULAR, s);
    glMaterialf(GL_FRONT, GL_SHININESS, 24);
    glColor3f(r, g, b);
}

static void box(float x, float y, float z, float w, float h, float d) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glScalef(w, h, d);
    glutSolidCube(1);
    glPopMatrix();
}

/* Ellipsoid: a sphere of radius r scaled by (sx,sy,sz) at (x,y,z). */
static void ball(float x, float y, float z, float r, int sl, int st, float sx = 1, float sy = 1, float sz = 1) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glScalef(sx, sy, sz);
    glutSolidSphere(r, sl, st);
    glPopMatrix();
}

/* Box with the texture repeated every 1.5 world units on each face. */
static void texturedBox(float x, float y, float z, float w, float h, float d, GLuint tex) {
    static const signed char N[6][3] = { {0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0} };
    static const signed char V[6][4][3] = {
        {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}},
        {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}},
        {{ 1,-1, 1},{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1}},
        {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}},
        {{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1},{-1, 1,-1}},
        {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}} };
    static const int U[6] = { 0,0,2,2,0,0 }, W[6] = { 1,1,1,1,2,2 }; /* size index for u / v */
    static const float T[4][2] = { {0,0},{1,0},{1,1},{0,1} };
    float dim[3] = { w,h,d };

    setColor(1, 1, 1);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBegin(GL_QUADS);
    for (int f = 0; f < 6; f++) {
        glNormal3f(N[f][0], N[f][1], N[f][2]);
        for (int k = 0; k < 4; k++) {
            glTexCoord2f(T[k][0] * dim[U[f]] / 1.5f, T[k][1] * dim[W[f]] / 1.5f);
            glVertex3f(x + V[f][k][0] * w / 2, y + V[f][k][1] * h / 2, z + V[f][k][2] * d / 2);
        }
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

static void shadowBlob(float x, float y, float z, float rx, float rz) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_LIGHTING);
    glColor4f(0, 0, 0, .20f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(x, y, z);
    for (int i = 0; i <= 24; i++) {
        float a = i * 2 * PI / 24;
        glVertex3f(x + cosf(a) * rx, y, z + sinf(a) * rz);
    }
    glEnd();
    glEnable(GL_LIGHTING);
    glDisable(GL_BLEND);
}

static void drawText(float x, float y, const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    glRasterPos2f(x, y);
    for (char* p = buf; *p; p++)glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *p);
}

/* Brightness of ceiling tube `id`: some are dead, the rest hum and now and
   then stutter out. */
static float lampGlow(int id) {
    float h = hash2(id, 7);
    if (h < .22f) return .06f;
    float g = .80f + .12f * sinf(hauntTime * (2.0f + h * 5.0f) + h * 40.0f);
    if (sinf(hauntTime * (.7f + h * 1.9f) + id * 1.7f) > .90f)
        g *= hash2(id, (int)(hauntTime * 24.0f)) > .45f ? .45f : .75f;
    return g;
}

/* Self-lit ceiling tube. */
static void drawLamp(float x, float y, float z, float w, float d, int id) {
    float g = lampGlow(id);
    glDisable(GL_LIGHTING);
    glColor3f(.98f * g, 1.0f * g, .86f * g);
    box(x, y, z, w, .06f, d);
    glEnable(GL_LIGHTING);
}

/* ------------------------------------------------------------ */
/* Building geometry (collision tables, built once)             */
/* ------------------------------------------------------------ */

/* Corridor is z=18..28. South classrooms (door at z=18) and north
   classrooms (door at z=28) share one layout, described per row:
   back wall, side walls, door-wall z, teacher desk, front desk, whiteboard,
   first chair row z. */
static const float ROW[2][12] = {
    /* back z0..z1  side z0..z1  doorZ  teacher      desk          board         chairZ */
    { 7.5f, 7.85f,  7.85f, 18.0f, 18.0f, 9.0f, 10.1f,  7.95f, 8.35f,  7.98f, 8.30f,  10.5f },
    { 38.0f,38.35f, 28.0f, 38.0f, 28.0f, 35.7f,36.8f,  37.25f,37.75f, 37.15f,37.48f, 34.2f } };

static void buildBuildingGeometry(void) {
    int i, s, r, col;
    char label[48];

    /* Outer boundary. */
    addWall(-24, -23.6f, 4, 52);
    addWall(23.6f, 24, 4, 52);
    addWall(-24, 24, 3.6f, 4);
    addWall(-24, 24, 51.6f, 52);

    /* Four classrooms per side, each fully enclosed with a 2.2m door opening. */
    for (s = 0; s < 2; s++)for (i = 0; i < 4; i++) {
        const float* R = ROW[s];
        float c = ROOM_X[i];
        addWall(c - 4, c + 4, R[0], R[1]);
        addWall(c - 4, c - 3.7f, R[2], R[3]);
        addWall(c + 3.7f, c + 4, R[2], R[3]);
        addWall(c - 4, c - 1.1f, R[4] - .15f, R[4] + .15f);
        addWall(c + 1.1f, c + 4, R[4] - .15f, R[4] + .15f);
        addObstacle(c - 3.20f, c - 2.10f, R[5], R[6]);
        addObstacle(c - 1.70f, c + 1.70f, R[7], R[8]);
        addObstacle(c - 2.95f, c + 2.95f, R[9], R[10]); /* whiteboard */
        for (r = 0; r < 3; r++)for (col = 0; col < 2; col++) {
            float sx = c - .95f + col * 1.9f, sz = R[11] + (s ? -1.8f : 1.8f) * r;
            addObstacle(sx - .50f, sx + .50f, sz - .55f, sz + .55f);
        }
    }

    /* Elevator shafts. */
    addWall(-7.8f, -7.5f, 41.0f, 49.0f);
    addWall(-4.0f, -3.7f, 41.0f, 49.0f);
    addWall(-7.8f, -3.7f, 48.70f, 49.0f);
    addWall(-7.80f, -7.30f, 41.00f, 41.30f);
    addWall(-6.00f, -5.50f, 41.00f, 41.30f);
    addWall(-4.20f, -3.70f, 41.00f, 41.30f);

    /* Washroom shell. */
    addWall(-3.0f, -2.70f, 41.0f, 49.0f);
    addWall(7.20f, 7.50f, 41.0f, 49.0f);
    addWall(-3.0f, 7.50f, 48.70f, 49.0f);
    addWall(-3.0f, 1.40f, 40.85f, 41.15f);
    addWall(3.60f, 7.50f, 40.85f, 41.15f);

    /* Washroom cubicle partitions, commodes, and the basin counter. */
    for (i = 0; i < 3; i++) {
        float x = .10f + i * 2.40f;
        addObstacle(x - 1.10f, x - 1.00f, 44.08f, 48.28f);
        addObstacle(x + 1.00f, x + 1.10f, 44.08f, 48.28f);
        addObstacle(x - .50f, x + .50f, 45.72f, 46.95f);
    }
    addObstacle(6.22f, 7.30f, 41.98f, 43.83f);

    /* Doors (order matters: the story lift must be index 12). */
    for (i = 0; i < 8; i++) {
        snprintf(label, sizeof(label), "Room %d", 101 + i);
        addDoor(ROOM_X[i % 4], 1.55f, i < 4 ? 18.0f : 28.0f, label, 0);
    }
    addDoor(2.50f, 1.55f, 41.0f, "Washroom", 2);
    for (i = 0; i < 3; i++) {
        snprintf(label, sizeof(label), "Toilet %d", i + 1);
        addDoor(.10f + i * 2.40f, 1.20f, 44.02f, label, 3);
    }
    addDoor(-6.65f, 1.55f, 41.0f, "Elevator A (Closed)", 1);
}

/* Floor, ceiling and ceiling lights. */
static void drawBuildingShell(void) {
    setColor(1, 1, 1);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tileTex);
    glBegin(GL_QUADS);
    glNormal3f(0, 1, 0);
    glTexCoord2f(0, 0);   glVertex3f(-23, .06f, 4);
    glTexCoord2f(23, 0);  glVertex3f(23, .06f, 4);
    glTexCoord2f(23, 24); glVertex3f(23, .06f, 52);
    glTexCoord2f(0, 24);  glVertex3f(-23, .06f, 52);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    setColor(.30f, .29f, .27f);
    box(0, -.03f, 28, 46, .18f, .04f);
    box(0, -.03f, 52, 46, .18f, .04f);
    box(-23, -.03f, 28, .04f, .18f, 48);
    box(23, -.03f, 28, .04f, .18f, 48);

    setColor(.36f, .35f, .32f); /* dingy ceiling */
    box(0, 3.16f, 28, 46, .10f, 48);

    for (int k = 0; k < 3; k++)for (int i = -2; i <= 2; i++)
        drawLamp(i * 8.0f, 3.08f, 14.0f + k * 9, 1.5f, .55f, k * 5 + i + 2);
    drawLamp(-5.8f, 3.08f, 45.0f, 1.5f, .55f, 20);
    drawLamp(4.8f, 3.08f, 45.0f, 1.5f, .55f, 21);
}

static void drawWallList(void) {
    for (int i = 0; i < wallCount; i++) {
        const Rect* w = &walls[i];
        float cx = (w->minx + w->maxx) / 2, cz = (w->minz + w->maxz) / 2;
        float sx = w->maxx - w->minx, sz = w->maxz - w->minz;
        if (sx < 0.5f) sx = 0.32f;
        if (sz < 0.5f) sz = 0.32f;
        texturedBox(cx, 1.5f, cz, sx, 3.0f, sz, wallTex);
        setColor(.20f, .17f, .14f); /* dark skirting board */
        box(cx, .075f, cz, sx + .04f, .12f, sz + .04f);
    }
}

/* ------------------------------------------------------------ */
/* Doors, elevators, fans, window                                */
/* ------------------------------------------------------------ */

/* Decorative face of a classroom/washroom door leaf; s = +1 corridor side,
   -1 room side. */
static void drawDoorFace(float s) {
    setColor(.43f, .26f, .14f);
    box(-.70f, .55f, .095f * s, .05f, 1.12f, .025f);
    box(.70f, .55f, .095f * s, .05f, 1.12f, .025f);
    box(0, 1.10f, .095f * s, 1.40f, .05f, .025f);
    box(0, -.02f, .095f * s, 1.40f, .05f, .025f);
    box(0, -1.02f, .095f * s, 1.40f, .05f, .025f);

    /* Narrow tall dark glass slit. */
    setColor(.10f, .12f, .12f); box(.34f, .28f, .105f * s, .26f, 1.20f, .028f);
    setColor(.22f, .30f, .31f); box(.34f, .28f, .12f * s, .19f, 1.10f, .012f);
    setColor(.42f, .50f, .50f); box(.30f, .62f, .126f * s, .05f, .42f, .006f);

    /* Nameplate and notice. */
    setColor(.07f, .07f, .07f); box(.34f, 1.14f, .12f * s, .46f, .17f, .025f);
    setColor(.86f, .84f, .78f); box(.34f, 1.14f, .14f * s, .36f, .10f, .008f);
    setColor(.90f, .89f, .85f); box(.34f, .86f, .13f * s, .34f, .30f, .006f);

    /* Round knob. */
    setColor(.80f, .80f, .82f); ball(-.18f, -.05f, .19f * s, .075f, 16, 12);
    setColor(.55f, .55f, .58f); ball(-.18f, -.05f, .15f * s, .045f, 12, 8);
}

static void drawWashroomCubicleDoor(const Door* d) {
    glPushMatrix();
    glTranslatef(d->x - 1.02f, d->y, d->z); /* left hinge, swings into the cubicle */
    glRotatef(d->angle, 0, 1, 0);
    glTranslatef(1.02f, 0, 0);

    setColor(.36f, .23f, .12f);
    box(-1.06f, 1.20f, 0, .08f, 2.50f, .16f);
    box(1.06f, 1.20f, 0, .08f, 2.50f, .16f);
    box(0, 2.45f, 0, 2.20f, .10f, .16f);

    setColor(.52f, .34f, .18f); box(0, 0, 0, 2.04f, 2.35f, .08f);   /* slab */
    setColor(.24f, .24f, .24f); box(.20f, .82f, .045f, .82f, .38f, .012f); /* vent gap */
    setColor(.48f, .48f, .50f); ball(.72f, -.05f, -.065f, .085f, 18, 14, 1, 1, .45f);
    setColor(.68f, .68f, .70f); ball(.72f, -.05f, -.045f, .045f, 14, 10, 1, 1, .35f);
    glPopMatrix();
}

static void drawWoodDoor(const Door* d) {
    glPushMatrix();
    glTranslatef(d->x, d->y, d->z);

    /* Fixed frame. */
    setColor(.52f, .36f, .19f);
    box(-1.16f, 0, 0, .14f, 3.08f, .28f);
    box(1.16f, 0, 0, .14f, 3.08f, .28f);
    box(0, 1.51f, 0, 2.46f, .18f, .28f);
    setColor(.40f, .27f, .14f);
    for (int sx = -1; sx <= 1; sx += 2)for (int sy = -1; sy <= 1; sy += 2)
        box(1.06f * sx, .78f * sy, .15f, .06f, .18f, .06f);
    box(0, 1.60f, .02f, 2.56f, .10f, .34f);

    /* Hinged leaf, decorated on both faces. */
    glTranslatef(-1.08f, 0, .03f);
    glRotatef(d->angle, 0, 1, 0);
    glTranslatef(1.08f, 0, 0);
    texturedBox(0, -.02f, .03f, 2.16f, 2.76f, .11f, woodTex);
    drawDoorFace(+1.0f);
    drawDoorFace(-1.0f);
    glPopMatrix();
}

/* Elevator front. Only the story lift's panels slide (openAmount 0..1);
   while open, a dark shaft shows behind them. */
static void drawElevator(float x, float z, float openAmount, int isStoryLift) {
    setColor(.22f, .23f, .24f); box(x, 1.55f, z, 1.55f, 3.15f, .36f);
    setColor(.08f, .09f, .10f); box(x, 1.45f, z - .20f, 1.30f, 2.86f, .08f);

    if (isStoryLift && openAmount > 0.02f) {
        glDisable(GL_LIGHTING);
        glColor3f(.02f, .02f, .03f);
        box(x, 1.45f, z - .30f, 1.22f, 2.80f, .05f);
        box(x - .62f, 1.45f, z - .20f, .05f, 2.80f, .36f);
        box(x + .62f, 1.45f, z - .20f, .05f, 2.80f, .36f);
        box(x, 2.83f, z - .20f, 1.22f, .05f, .36f);
        glEnable(GL_LIGHTING);
    }

    float slide = openAmount * 0.30f;
    texturedBox(x - .30f - slide, 1.45f, z - .24f, .58f, 2.78f, .035f, metalTex);
    texturedBox(x + .30f + slide, 1.45f, z - .24f, .58f, 2.78f, .035f, metalTex);

    setColor(.10f, .10f, .11f); box(x, 2.96f, z - .24f, 1.42f, .10f, .06f);

    /* Status light: red while the story lift is locked, green otherwise. */
    if (isStoryLift && liftState == 0) setColor(.68f, .16f, .13f);
    else setColor(.25f, .60f, .38f);
    box(x, 2.75f, z - .27f, .25f, .16f, .04f);

    /* Call panel. */
    setColor(.08f, .08f, .08f); box(x + .92f, 1.55f, z - .20f, .30f, 1.05f, .08f);
    setColor(.78f, .78f, .72f); box(x + .92f, 2.06f, z - .25f, .18f, .20f, .03f);
    setColor(.78f, .62f, .20f); box(x + .92f, 1.60f, z - .25f, .13f, .16f, .03f);
}

/* Small continuously-rotating ceiling fan. */
static void drawCeilingFan(float x, float y, float z) {
    glPushMatrix();
    glTranslatef(x, y, z);
    setColor(.15f, .15f, .16f); box(0, .10f, 0, .05f, .20f, .05f); /* rod */
    setColor(.20f, .20f, .22f); box(0, 0, 0, .22f, .10f, .22f);    /* hub */
    glRotatef(fanAngle, 0, 1, 0);
    setColor(.80f, .80f, .78f);
    for (int b = 0; b < 4; b++) {
        box(.32f, 0, 0, .58f, .02f, .11f);
        glRotatef(90.0f, 0, 1, 0);
    }
    glPopMatrix();
}

/* Framed window showing the painted outdoor scene; purely decorative. */
static void drawWindowView(float x, float y, float z, float rotY) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(rotY, 0, 1, 0);

    setColor(.18f, .12f, .07f);
    box(0, 0, 0, 3.6f, 2.6f, .12f);

    setColor(1, 1, 1);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);
    /* v=0 is the sky row, so it goes on the top edge. */
    glTexCoord2f(0, 1); glVertex3f(-1.65f, -1.15f, .07f);
    glTexCoord2f(1, 1); glVertex3f(1.65f, -1.15f, .07f);
    glTexCoord2f(1, 0); glVertex3f(1.65f, 1.15f, .07f);
    glTexCoord2f(0, 0); glVertex3f(-1.65f, 1.15f, .07f);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    setColor(.30f, .20f, .11f); /* mullions */
    box(0, 0, .09f, .08f, 2.3f, .05f);
    box(0, 0, .09f, 3.3f, .08f, .05f);
    glPopMatrix();
}

/* ------------------------------------------------------------ */
/* Classroom and washroom interiors                             */
/* ------------------------------------------------------------ */

static void drawClassroom(float cx, float cz) {
    int south = cz < 20.0f;
    float s = south ? -1.0f : 1.0f; /* toward the back (whiteboard) wall */

    glPushMatrix();
    glTranslatef(cx, 0, cz);

    /* Old chalkboard and teacher desk at the back, away from the door. */
    setColor(.25f, .17f, .10f); box(0, 1.72f, 4.37f * s, 6.1f, 1.72f, .06f);
    setColor(.10f, .17f, .13f); box(0, 1.72f, 4.35f * s, 5.9f, 1.55f, .08f);
    setColor(.62f, .64f, .58f); /* half-erased chalk scribbles */
    for (int row = 0; row < 4; row++) {
        float lx = -2.5f;
        for (int w = 0; w < 6; w++) {
            float h = hash2((int)(cx * 3) + row * 11, w + (int)cz), len = .18f + h * .55f;
            if (h > .25f) box(lx + len / 2, 2.22f - row * .30f, 4.30f * s, len, .035f, .012f);
            lx += len + .15f;
            if (lx > 2.5f) break;
        }
    }
    setColor(.16f, .16f, .16f); box(0, 1.00f, 4.28f * s, 5.65f, .08f, .05f);
    setColor(.18f, .18f, .19f); box(-2.65f, .55f, 3.25f * s, .95f, 1.05f, .65f);
    setColor(.66f, .50f, .32f); box(-2.65f, 1.10f, 3.25f * s, 1.05f, .10f, .70f);

    /* 3x3 abandoned asylum beds, one per old chair slot; south rooms are
       rotated 180 degrees so the headboards face the old whiteboard wall. */
    for (int r = 0; r < 3; r++)for (int c = 0; c < 3; c++) {
        float x = -1.8f + c * 1.8f, z = s * (2.2f - r * 1.85f);
        float rusth = hash2(r * 7 + c, (int)(cx + cz) * 3); /* per-bed grime variety */
        glPushMatrix();
        glTranslatef(x, 0, z);
        if (south) glRotatef(180.0f, 0, 1, 0);

        setColor(.20f + rusth * .06f, .16f, .13f); /* rusted tubular bed frame legs */
        for (int lx = -1; lx <= 1; lx += 2)for (int lz = -1; lz <= 1; lz += 2)
            box(.32f * lx, .23f, .20f * lz, .05f, .46f, .05f);
        box(0, .44f, 0, .74f, .04f, .50f); /* frame rail */

        setColor(.34f + rusth * .08f, .19f, .12f); /* rusty metal headboard bars */
        box(0, .82f, -.28f, .72f, .62f, .04f);
        for (int bar = -3; bar <= 3; bar++)
            box(bar * .10f, .82f, -.30f, .025f, .70f, .025f);
        box(0, .78f, .28f, .72f, .30f, .04f); /* low footboard */

        setColor(.55f - rusth * .12f, .53f, .48f); /* stained, sagging mattress */
        box(0, .50f, 0, .70f, .10f, .58f);
        setColor(.42f - rusth * .10f, .36f, .30f); /* old stains */
        ball(-.14f, .555f, .12f, .10f, 8, 6, 1.5f, .3f, 1.0f);
        ball(.10f, .555f, -.16f, .08f, 8, 6, 1.3f, .3f, .9f);

        setColor(.62f, .60f, .54f); /* thin balled-up pillow */
        box(0, .58f, -.18f, .40f, .08f, .18f);

        setColor(.30f, .27f, .10f); /* a tangled, half-fallen sheet */
        box(.20f, .47f, .05f, .30f, .03f, .70f);
        box(.36f, .30f, .05f, .06f, .30f, .40f);

        setColor(.28f + rusth * .10f, .27f, .25f); /* leaning rusty IV stand */
        glPushMatrix();
        glTranslatef(.55f, 0, -.05f);
        glRotatef(8.0f, 0, 0, 1);
        box(0, .55f, 0, .04f, 1.10f, .04f);
        box(0, .04f, 0, .32f, .04f, .32f);
        setColor(.55f, .58f, .55f);
        ball(0, 1.05f, 0, .08f, 10, 8);
        glPopMatrix();
        glPopMatrix();
    }

    drawLamp(0, 2.98f, 0, 1.4f, .65f, 40 + (int)(cx + cz));
    for (int fx = -1; fx <= 1; fx += 2)for (int fz = -1; fz <= 1; fz += 2)
        drawCeilingFan(1.5f * fx, 2.85f, 1.3f * fz);

    shadowBlob(0, .03f, 0, 3.1f, 3.8f);
    glPopMatrix();
}

/* Three toilet cubicles with high commodes, two basins and a mirror.
   The cubicle doors themselves are drawn with the other doors. */
static void drawWashroom(float cx, float cz) {
    int i;
    texturedBox(cx, 1.50f, cz + 3.05f, 7.0f, 2.95f, .08f, tileTex);

    for (i = 0; i < 3; i++) {
        float sx = cx - 2.40f + i * 2.40f;
        setColor(.82f, .82f, .80f);
        box(sx - 1.05f, 1.35f, cz + 1.18f, .10f, 2.60f, 4.20f);
        box(sx + 1.05f, 1.35f, cz + 1.18f, .10f, 2.60f, 4.20f);
        setColor(.72f, .72f, .70f); box(sx, 2.60f, cz + 1.18f, 2.10f, .10f, 4.20f);
        setColor(.93f, .93f, .91f); box(sx, .42f, cz + 1.45f, .72f, .70f, .80f);
        ball(sx, .83f, cz + 1.18f, 1.0f, 24, 16, .56f, .25f, .62f); /* bowl */

        setColor(.82f, .82f, .80f); /* seat */
        glPushMatrix();
        glTranslatef(sx, 1.02f, cz + 1.18f);
        glRotatef(90, 1, 0, 0);
        glutSolidTorus(.055f, .38f, 12, 24);
        glPopMatrix();

        setColor(.90f, .90f, .88f); box(sx, 1.15f, cz + 1.72f, .62f, 1.10f, .28f); /* tank */
        setColor(.55f, .55f, .57f); ball(sx, 1.48f, cz + 1.56f, .055f, 12, 8);    /* flush */
    }

    const float basinX = 6.75f, mirrorX = 7.10f;
    setColor(.72f, .72f, .70f);
    box(basinX, .82f, cz - 2.10f, 1.05f, .16f, 1.85f);
    for (i = 0; i < 2; i++) {
        float bz = cz - 2.55f + i * .90f;
        setColor(.94f, .94f, .92f); ball(basinX, 1.00f, bz, 1.0f, 24, 16, .42f, .18f, .34f);
        setColor(.48f, .49f, .50f); ball(basinX, 1.08f, bz, .055f, 12, 8); /* drain */
        setColor(.62f, .63f, .65f); /* faucet */
        box(basinX, 1.28f, bz + .22f, .08f, .48f, .08f);
        box(basinX, 1.50f, bz + .12f, .08f, .08f, .26f);
    }
    setColor(.28f, .28f, .29f); box(mirrorX, 2.05f, cz - 2.10f, .08f, 1.55f, 1.85f);
    setColor(.12f, .16f, .17f); box(mirrorX - .05f, 2.05f, cz - 2.10f, .025f, 1.35f, 1.65f); /* murky mirror */

    drawLamp(cx, 3.00f, cz + 0.20f, 1.30f, .55f, 60);
    shadowBlob(cx, .03f, cz, 3.2f, 2.8f);
    setColor(.36f, .35f, .32f); box(cx, 3.05f, cz, 7.0f, .08f, 6.9f);
}

/* ------------------------------------------------------------ */
/* Zombie NPCs: AI                                              */
/* ------------------------------------------------------------ */

static GLUquadric* zombieQuad = NULL;
static float zombieAnimTime = 0.0f;

typedef struct {
    float x, z;
    int zone;       /* 0 Room 101, 1 Room 105, 2 Room 108, 3 left edge, 4 right edge, 5 washroom */
    int pathIndex;
    float phase;
    int health;     /* hits left before it falls */
    int state;      /* 0 alive, 1 dying, 2 dead/respawning */
    float stateTimer;
} SEUZombieNPC;

#define ZOMBIE_COUNT 6
static SEUZombieNPC seuZombies[ZOMBIE_COUNT] = {
    {-19.20f,16.20f,0,1,0.30f,3},
    {-19.00f,34.40f,1,0,1.40f,3},
    { 13.00f,34.40f,2,0,2.60f,3},
    {-21.50f, 6.00f,3,1,3.70f,3},
    { 21.50f, 6.00f,4,2,4.80f,3},
    {  0.00f,42.20f,5,1,5.90f,3}
};

/* Within this range a zombie drops its patrol and charges the shooter. */
static const float ZOMBIE_AGGRO_RADIUS = 6.5f;
static const float ZOMBIE_CHASE_STEP = 0.030f;

/* Zombie-only blockers for the drawn classroom/washroom furniture, so they
   never walk through chairs, desks, boards, partitions or commodes. */
static int zombieFurnitureBlocked(float x, float z, float rad) {
    int i, r, c;
    for (int s = -1; s <= 1; s += 2)for (i = 0; i < 4; i++) {
        float cx = ROOM_X[i], cz = s < 0 ? 12.5f : 33.0f, bz = cz + s * 4.35f;
        float d0 = cz + s * 2.90f, d1 = cz + s * 3.60f;
        if (rectHit(x, z, rad, cx - 2.95f, cx + 2.95f, bz - .08f, bz + .08f)) return 1;
        if (rectHit(x, z, rad, cx - 3.15f, cx - 2.15f, fminf(d0, d1), fmaxf(d0, d1))) return 1;
        for (r = 0; r < 3; r++)for (c = 0; c < 3; c++) {
            float fx = cx - 1.8f + c * 1.8f, fz = cz + s * (2.2f - r * 1.85f);
            if (rectHit(x, z, rad, fx - .55f, fx + .55f, fz - .42f, fz + .42f)) return 1;
        }
    }
    for (i = 0; i < 3; i++) {
        float sx = .10f + i * 2.40f;
        if (rectHit(x, z, rad, sx - 1.10f, sx - 1.00f, 44.08f, 48.28f)) return 1;
        if (rectHit(x, z, rad, sx + 1.00f, sx + 1.10f, 44.08f, 48.28f)) return 1;
        if (rectHit(x, z, rad, sx - .50f, sx + .50f, 45.83f, 47.07f)) return 1;
    }
    return rectHit(x, z, rad, 6.22f, 7.30f, 41.95f, 43.85f);
}

/* Hard safety box for zombies that belong to a room (not zones 3/4). */
static const float ZONE_BOUNDS[6][4] = {
    {-19.45f,-12.55f, 8.10f,17.60f},  /* Room 101 */
    {-19.45f,-12.55f,28.40f,37.75f},  /* Room 105 */
    { 12.55f, 19.45f,28.40f,37.75f},  /* Room 108 */
    {0},{0},
    { -2.45f,  6.95f,41.40f,48.45f} }; /* Washroom */

static int zombieBlocked(int zone, float x, float z) {
    const float ZR = .22f;
    if (collides(x, z) || zombieFurnitureBlocked(x, z, ZR)) return 1;
    if (x < -23.20f + ZR || x > 23.20f - ZR || z < 4.20f + ZR || z > 51.40f - ZR) return 1;
    if (zone == 3 || zone == 4) return 0;
    const float* b = ZONE_BOUNDS[zone];
    return x < b[0] || x > b[1] || z < b[2] || z > b[3];
}

/* Patrol waypoints through aisles and open space. */
static void zombieTargetFor(const SEUZombieNPC* z, float* tx, float* tz) {
    static const float r101X[4] = { -3.20f,3.20f,3.20f,-3.20f }, r101Z[4] = { 3.70f,3.70f,-1.30f,-1.30f };
    static const float roomX[4] = { -3.00f,3.00f,3.00f,-3.00f }, roomZ[4] = { 1.40f,1.40f,-2.70f,-2.70f };
    static const float edgeZ[4] = { 6.0f,17.0f,29.0f,40.5f };
    static const float washX[4] = { 0.00f,5.35f,5.35f,0.00f }, washZ[4] = { 42.20f,42.20f,43.35f,43.35f };
    int p = z->pathIndex % 4;
    switch (z->zone) {
    case 0:  *tx = -16.0f + r101X[p]; *tz = 12.5f + r101Z[p]; break;
    case 1:
    case 2:  *tx = (z->zone == 1 ? -16.0f : 16.0f) + roomX[p]; *tz = 33.0f + roomZ[p]; break;
    case 3:
    case 4:  *tx = z->zone == 3 ? -21.50f : 21.50f; *tz = edgeZ[p]; break;
    default: *tx = washX[p]; *tz = washZ[p];
    }
}

static void nextWaypoint(SEUZombieNPC* z) { z->pathIndex = (z->pathIndex + 1) % 4; }

static void updateSEUZombies(void) {
    for (int i = 0; i < ZOMBIE_COUNT; i++) {
        SEUZombieNPC* z = &seuZombies[i];

        if (z->state != 0) {  /* 1 s fall, then 6 s hidden, then respawn */
            z->stateTimer += 0.016f;
            if (z->state == 1 && z->stateTimer > 1.0f) { z->state = 2; z->stateTimer = 0.0f; }
            else if (z->state == 2 && z->stateTimer > 6.0f) { z->state = 0; z->health = 3; z->stateTimer = 0.0f; }
            continue;
        }

        float pdx = camX - z->x, pdz = camZ - z->z;
        float pdist = sqrtf(pdx * pdx + pdz * pdz);
        if (pdist < ZOMBIE_AGGRO_RADIUS && pdist > 0.001f) {
            float mx = pdx / pdist * ZOMBIE_CHASE_STEP, mz = pdz / pdist * ZOMBIE_CHASE_STEP;
            if (!zombieBlocked(z->zone, z->x + mx, z->z)) z->x += mx;
            if (!zombieBlocked(z->zone, z->x, z->z + mz)) z->z += mz;
            continue;
        }

        float tx, tz;
        zombieTargetFor(z, &tx, &tz);
        float dx = tx - z->x, dz = tz - z->z, dist = sqrtf(dx * dx + dz * dz);
        if (dist < 0.18f) {
            nextWaypoint(z);
            zombieTargetFor(z, &tx, &tz);
            dx = tx - z->x; dz = tz - z->z; dist = sqrtf(dx * dx + dz * dz);
        }
        if (dist <= .001f) continue;

        const float step = .0125f;
        if (z->zone == 1 || z->zone == 2) {
            /* Axis-by-axis patrol: no diagonal corner-cutting. */
            int moved = 0;
            if (fabsf(dx) > 0.08f) {
                float mx = (dx > 0.0f) ? step : -step;
                if (!zombieBlocked(z->zone, z->x + mx, z->z)) { z->x += mx; moved = 1; }
            }
            else if (fabsf(dz) > 0.08f) {
                float mz = (dz > 0.0f) ? step : -step;
                if (!zombieBlocked(z->zone, z->x, z->z + mz)) { z->z += mz; moved = 1; }
            }
            if (!moved) nextWaypoint(z);
        }
        else {
            float mx = dx / dist * step, mz = dz / dist * step;
            if (!zombieBlocked(z->zone, z->x + mx, z->z)) z->x += mx;
            else nextWaypoint(z);
            if (!zombieBlocked(z->zone, z->x, z->z + mz)) z->z += mz;
        }
    }
}

/* ------------------------------------------------------------ */
/* Zombie NPCs: hierarchical model                              */
/* ------------------------------------------------------------ */

/* Colors are applied with glColor3fv (GL_COLOR_MATERIAL drives lighting).
   Palette reworked for a scavenged-robot look: brushed steel plating,
   dark joint housings, a glowing red optic sensor, rust/oil streaks
   instead of blood, and a metal claw hand. */
static const float SKIN[3] = { 0.58f, 0.60f, 0.62f };
static const float SKIN_DARK[3] = { 0.34f, 0.36f, 0.38f };
static const float SHIRT[3] = { 0.30f, 0.32f, 0.35f };
static const float PANTS[3] = { 0.22f, 0.23f, 0.26f };
static const float BOOT[3] = { 0.10f, 0.10f, 0.11f };
static const float BLOOD[3] = { 0.16f, 0.13f, 0.06f };
static const float BLOOD_DK[3] = { 0.09f, 0.07f, 0.03f };
static const float EYE_GLOW[3] = { 1.00f, 0.12f, 0.08f };
static const float TEETH[3] = { 0.66f, 0.68f, 0.70f };
static const float CLAW[3] = { 0.12f, 0.12f, 0.13f };

/* Deterministic blood blobs: x, y, z, radius in the body part's space. */
static const float headBlood[6][4] = {
    { 0.35f, 0.10f,0.55f,0.07f},{-0.20f,-0.25f,0.58f,0.06f},{ 0.05f,-0.40f,0.55f,0.05f},
    {-0.45f, 0.30f,0.40f,0.05f},{ 0.50f,-0.10f,0.30f,0.06f},{-0.10f, 0.55f,0.35f,0.04f} };
static const float torsoBlood[10][4] = {
    {-0.45f, 0.55f, 0.44f,0.10f},{ 0.30f, 0.35f, 0.45f,0.08f},{ 0.10f,-0.10f, 0.46f,0.11f},
    {-0.20f,-0.35f, 0.44f,0.09f},{ 0.40f,-0.55f, 0.42f,0.12f},{-0.10f,-0.70f, 0.40f,0.10f},
    { 0.15f,-0.85f, 0.38f,0.13f},{-0.35f,-0.80f, 0.36f,0.10f},{ 0.00f, 0.10f,-0.44f,0.08f},
    {-0.30f,-0.40f,-0.42f,0.09f} };
static const float limbBlood[3][4] = {
    { 0.05f,-0.20f,0.15f,0.06f},{-0.08f,-0.55f,0.14f,0.05f},{ 0.10f,-0.85f,0.13f,0.05f} };

static void zombieBlood(const float arr[][4], int count) {
    glColor3fv(BLOOD);
    for (int i = 0; i < count; ++i) ball(arr[i][0], arr[i][1], arr[i][2], arr[i][3], 8, 6);
}

/* Capped cylinder limb segment. */
static void zombieLimb(float radiusTop, float radiusBottom, float length) {
    glPushMatrix();
    glRotatef(-90.0f, 1, 0, 0);
    gluCylinder(zombieQuad, radiusTop, radiusBottom, length, 10, 4);
    gluDisk(zombieQuad, 0, radiusTop, 10, 2);
    glPopMatrix();
}

static void zombieClawFinger(float spreadX, float droop) {
    glPushMatrix();
    glTranslatef(spreadX, -0.05f, 0.02f);
    glRotatef(droop, 1, 0, 0);
    glColor3fv(SKIN);
    box(0, -0.14f, 0, 0.05f, 0.28f, 0.05f);
    glTranslatef(0, -0.30f, 0);
    glColor3fv(CLAW);
    glRotatef(90, 1, 0, 0);
    glutSolidCone(0.035f, 0.12f, 8, 2);
    glPopMatrix();
}

static void zombieHand(void) {
    glColor3fv(SKIN);
    box(0, 0, 0, 0.26f, 0.16f, 0.14f);
    zombieClawFinger(-0.16f, 60.0f);
    zombieClawFinger(-0.05f, 68.0f);
    zombieClawFinger(0.06f, 68.0f);
    zombieClawFinger(0.17f, 60.0f);

    glPushMatrix(); /* thumb */
    glTranslatef(-0.18f, 0.02f, 0.05f);
    glRotatef(80.0f, 0, 0, 1);
    glColor3fv(SKIN);
    box(0, -0.10f, 0, 0.045f, 0.20f, 0.045f);
    glPopMatrix();

    zombieBlood(limbBlood, 1);
}

static void zombieHead(void) {
    int i;
    glColor3fv(SKIN);      box(0, 0, 0, 1.5f, 1.55f, 1.25f);        /* skull / head shell */
    glColor3fv(SKIN_DARK); box(0, 0.35f, 0.60f, 1.3f, 0.18f, 0.1f); /* brow plate */
    for (i = -1; i <= 1; i += 2) box(0.33f * i, 0.08f, 0.62f, 0.34f, 0.24f, 0.10f); /* eye housings */

    /* Single glowing red optic sensor, centred like the reference robot. */
    GLfloat emission[] = { EYE_GLOW[0], EYE_GLOW[1], EYE_GLOW[2], 1.0f }, noEmission[] = { 0,0,0,1 };
    glMaterialfv(GL_FRONT, GL_EMISSION, emission);
    glColor3fv(EYE_GLOW);
    ball(0.0f, 0.08f, 0.70f, 0.13f, 14, 10);
    glMaterialfv(GL_FRONT, GL_EMISSION, noEmission);
    glColor3fv(SKIN_DARK);
    for (i = -1; i <= 1; i += 2) ball(0.33f * i, 0.08f, 0.68f, 0.045f, 8, 6); /* small side bolts */

    glColor3fv(BLOOD_DK); box(0, -0.35f, 0.60f, 0.75f, 0.22f, 0.12f); /* vented mouth grille */
    glColor3fv(TEETH);
    for (i = -3; i <= 3; ++i) box(i * 0.10f, -0.27f, 0.66f, 0.06f, 0.10f, 0.05f);
    glColor3fv(BLOOD); /* oil drips instead of blood */
    for (i = -1; i <= 1; ++i) box(i * 0.18f, -0.55f - 0.05f * i, 0.60f, 0.05f, 0.35f, 0.05f);

    /* Four antennas splayed from the top of the head, each a two-segment
       rod with a glowing tip, like the reference robot's wire whiskers.
       Sized up so they read clearly from a distance. */
    {
        static const float antX[4] = { -0.50f, 0.50f, -0.34f, 0.34f };
        static const float antZ[4] = { 0.28f, 0.28f, -0.22f, -0.22f };
        static const float antTiltZ[4] = { -20.0f, 20.0f, -11.0f, 11.0f };
        static const float antTiltX[4] = { 8.0f, 8.0f, -10.0f, -10.0f };
        for (i = 0; i < 4; ++i) {
            glPushMatrix();
            glTranslatef(antX[i], 0.78f, antZ[i]);
            glRotatef(antTiltZ[i], 0, 0, 1);
            glRotatef(antTiltX[i], 1, 0, 0);
            glColor3fv(SKIN_DARK);
            box(0.0f, 0.16f, 0.0f, 0.060f, 0.32f, 0.060f);
            glTranslatef(0.0f, 0.30f, 0.0f);
            box(0.0f, 0.12f, 0.0f, 0.045f, 0.24f, 0.045f);
            glTranslatef(0.0f, 0.22f, 0.0f);
            glColor3fv(EYE_GLOW);
            ball(0.0f, 0.09f, 0.0f, 0.085f, 10, 8);
            glPopMatrix();
        }
    }

    zombieBlood(headBlood, 6);
}

static void zombieTorso(void) {
    glColor3fv(SHIRT);
    box(0, 0, 0, 1.55f, 1.9f, 0.85f);
    glColor3fv(SKIN_DARK); /* chest panel seams */
    for (int i = -2; i <= 2; ++i)
        box(i * 0.3f, -0.95f - (i % 2 == 0 ? 0.10f : 0.0f), 0.40f, 0.22f, 0.35f, 0.15f);
    zombieBlood(torsoBlood, 10);
}

static void zombieArm(void) {
    glPushMatrix();
    glColor3fv(SKIN);
    zombieLimb(0.20f, 0.17f, 0.85f);
    glTranslatef(0.0f, -0.85f, 0.0f);
    zombieLimb(0.15f, 0.12f, 0.75f);
    glTranslatef(0.0f, -0.78f, 0.0f);
    zombieHand();
    zombieBlood(limbBlood, 3);
    glPopMatrix();
}

static void zombieLeg(void) {
    glPushMatrix();
    glColor3fv(PANTS);
    zombieLimb(0.26f, 0.22f, 1.0f);
    glTranslatef(0.0f, -1.0f, 0.0f);
    zombieLimb(0.20f, 0.16f, 0.9f);
    glTranslatef(0.0f, -0.95f, 0.10f);
    glColor3fv(BOOT);
    box(0, 0, 0, 0.32f, 0.22f, 0.55f);
    zombieBlood(limbBlood, 2);
    glPopMatrix();
}

/* Full zombie, lurching with one arm raised and one reaching forward. */
static void drawSEUZombieCharacter(float phase) {
    float t = zombieAnimTime, stride = sinf(t * 5.0f + phase) * 10.0f;
    glPushMatrix();
    glTranslatef(0.0f, sinf(t * 1.3f) * 0.03f, 0.0f); /* idle bob */

    glPushMatrix(); /* hips and legs */
    glTranslatef(0.0f, 0.45f, 0.0f);
    glColor3fv(PANTS);
    box(0, 0, 0, 1.45f, 0.5f, 0.8f);
    glPushMatrix();
    glTranslatef(-0.35f, -0.25f, 0.0f);
    glRotatef(22.0f + stride, 1, 0, 0);
    zombieLeg();
    glPopMatrix();
    glPushMatrix();
    glTranslatef(0.35f, -0.25f, 0.0f);
    glRotatef(-15.0f - stride, 1, 0, 0);
    zombieLeg();
    glPopMatrix();
    glPopMatrix();

    glPushMatrix(); /* torso, neck, head, arms */
    glTranslatef(0.0f, 1.55f, 0.0f);
    glRotatef(5.0f, 0, 1, 0);
    zombieTorso();

    glColor3fv(SKIN);
    glPushMatrix();
    glTranslatef(0.0f, 1.15f, 0.0f);
    zombieLimb(0.28f, 0.32f, 0.35f);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 1.55f, 0.05f);
    glRotatef(-6.0f, 0, 1, 0);
    glRotatef(10.0f, 1, 0, 0);
    zombieHead();
    glPopMatrix();

    glPushMatrix();
    glTranslatef(-1.02f, 0.60f, 0.0f);
    glRotatef(-95.0f + sinf(t * 0.9f) * 4.0f, 0, 0, 1);
    glRotatef(-20.0f, 1, 0, 0);
    zombieArm();
    glPopMatrix();

    glPushMatrix();
    glTranslatef(1.02f, 0.60f, 0.0f);
    glRotatef(60.0f + sinf(t * 1.1f + 1.0f) * 4.0f, 0, 0, 1);
    glRotatef(30.0f, 1, 0, 0);
    zombieArm();
    glPopMatrix();

    glPopMatrix();
    glPopMatrix();
}

static void drawSEUZombies(void) {
    for (int i = 0; i < ZOMBIE_COUNT; i++) {
        const SEUZombieNPC* z = &seuZombies[i];
        if (z->state == 2) continue; /* dead, waiting to respawn */

        float tx, tz;
        float pdx = camX - z->x, pdz = camZ - z->z, pdist = sqrtf(pdx * pdx + pdz * pdz);
        if (z->state == 0 && pdist < ZOMBIE_AGGRO_RADIUS && pdist > 0.001f) { tx = camX; tz = camZ; }
        else zombieTargetFor(z, &tx, &tz);
        float heading = atan2f(tx - z->x, tz - z->z) * 180.0f / PI;

        glPushMatrix();
        if (z->state == 1) { /* dying: topple backward and sink */
            glTranslatef(z->x, .58f - z->stateTimer * 0.3f, z->z);
            glRotatef(heading, 0, 1, 0);
            glRotatef(z->stateTimer * 90.0f, 1, 0, 0);
        }
        else {
            glTranslatef(z->x, .58f, z->z);
            glRotatef(heading, 0, 1, 0);
        }
        glScalef(.32f, .32f, .32f); /* ~1.8m tall */
        drawSEUZombieCharacter(z->phase);
        glPopMatrix();
    }
}

/* ------------------------------------------------------------ */
/* Gun viewmodel, shooting, game flow                           */
/* ------------------------------------------------------------ */

static void gunBox(float x, float y, float z, float w, float h, float d, float c1, float c2, float c3) {
    glColor3f(c1, c2, c3);
    box(x, y, z, w, h, d);
}

/* ---- futuristic pistol palette: dark gunmetal body + glowing cyan lines ----
   Sits next to the gunBox() helper above so every gun-color constant lives
   in one place. */
static const float GUN_BODY[3] = { 1.00f, 0.11f, 0.13f };  /* main dark gunmetal shell   */
static const float GUN_BODY_LT[3] = { 0.19f, 0.20f, 0.23f };  /* lighter raised top plate   */
static const float GUN_ACCENT[3] = { 0.55f, 0.58f, 0.62f };  /* brushed silver trim/studs  */
static const float GUN_GRIP[3] = { 0.07f, 0.07f, 0.08f };  /* textured black grip        */
static const float GUN_GLOW[3] = { 1.00f, 0.10f, 0.08f };  /* self-lit red energy lines */

/* Emissive box: glows cyan on its own regardless of scene lighting - same
   GL_EMISSION trick already used for the zombie's red eye in zombieHead().
   Used for every glowing strip/lens on the pistol. */
static void gunGlowBox(float x, float y, float z, float w, float h, float d) {
    GLfloat emission[] = { GUN_GLOW[0], GUN_GLOW[1], GUN_GLOW[2], 1.0f }, noEmission[] = { 0,0,0,1 };
    glMaterialfv(GL_FRONT, GL_EMISSION, emission); /* turn this surface into its own light source */
    glColor3fv(GUN_GLOW);
    box(x, y, z, w, h, d);
    glMaterialfv(GL_FRONT, GL_EMISSION, noEmission); /* reset emission so later objects don't glow */
}

/* Drawn in camera space: -Z forward, +X right, +Y up.
   Sci-fi hi-tech pistol: blocky angular gunmetal slide with a raised
   ridged top rail, a glowing cyan strip down its side, a lit front
   emitter lens, and a raked textured grip with its own glow seam. */
static void drawViewmodelGun(float bobX, float bobY) {
    glPushMatrix();
    glTranslatef(0.32f + bobX, -0.28f + bobY, -0.55f); /* view-space position + walk-bob offset */
    glRotatef(-8.0f, 0, 1, 0);                          /* slight yaw so the gun reads in 3D     */

    /* ---- slide / upper body ---- */
    gunBox(0, 0.045f, -0.20f, 0.11f, 0.075f, 0.34f, GUN_BODY[0], GUN_BODY[1], GUN_BODY[2]);        /* main slide block */
    gunBox(0, 0.085f, -0.10f, 0.095f, 0.02f, 0.20f, GUN_BODY_LT[0], GUN_BODY_LT[1], GUN_BODY_LT[2]); /* raised top plate */

    for (int i = 0; i < 5; i++) /* cooling-vent ridges along the top, like the reference photo */
        gunBox(-0.14f + i * 0.07f, 0.086f, -0.10f, 0.02f, 0.006f, 0.16f, GUN_ACCENT[0], GUN_ACCENT[1], GUN_ACCENT[2]);

    gunGlowBox(0, 0.045f, -0.16f, 0.010f, 0.022f, 0.30f);   /* signature glowing strip down the slide's side */
    gunGlowBox(0, 0.045f, -0.365f, 0.05f, 0.05f, 0.02f);    /* lit emitter lens at the muzzle */

    /* ---- lower receiver / frame ---- */
    gunBox(0, 0.0f, -0.02f, 0.10f, 0.09f, 0.20f, GUN_BODY[0], GUN_BODY[1], GUN_BODY[2]); /* frame body */
    gunGlowBox(0.052f, 0.0f, -0.02f, 0.006f, 0.05f, 0.14f);  /* thin glow accent on the frame's side */

    setColor(GUN_ACCENT[0], GUN_ACCENT[1], GUN_ACCENT[2]);
    ball(0, 0.06f, 0.04f, 0.028f, 10, 8);                    /* small silver tech disc near the rear */

    /* ---- raked, textured pistol grip ---- */
    glPushMatrix();
    glTranslatef(0.0f, -0.14f, 0.10f);
    glRotatef(24.0f, 1, 0, 0);                               /* angle the grip back like the reference */
    gunBox(0, 0, 0, 0.075f, 0.17f, 0.075f, GUN_GRIP[0], GUN_GRIP[1], GUN_GRIP[2]); /* grip body */
    for (int i = 0; i < 4; i++)                              /* horizontal texture ridges on the grip */
        gunBox(0, -0.05f + i * 0.035f, 0.039f, 0.07f, 0.008f, 0.006f,
            GUN_ACCENT[0] * .6f, GUN_ACCENT[1] * .6f, GUN_ACCENT[2] * .6f);
    gunGlowBox(0.035f, 0.02f, 0.0f, 0.006f, 0.10f, 0.05f);   /* glow seam down the grip's edge */
    glPopMatrix();

    /* ---- trigger guard + trigger ---- */
    gunBox(0, -0.05f, -0.03f, 0.07f, 0.012f, 0.09f, GUN_BODY[0], GUN_BODY[1], GUN_BODY[2]); /* guard bar */
    gunBox(-0.012f, -0.02f, -0.01f, 0.008f, 0.03f, 0.01f, GUN_BODY[0] * 0.6f, GUN_BODY[1] * 0.6f, GUN_BODY[2] * 0.6f);
    gunBox(0.012f, -0.02f, -0.01f, 0.008f, 0.03f, 0.01f, GUN_BODY[0] * 0.6f, GUN_BODY[1] * 0.6f, GUN_BODY[2] * 0.6f); /* trigger */

    /* muzzle flash: same mechanic as before, tinted red to match the new gun's glow */
    if (muzzleFlashTimer > 0.0f) {
        glDisable(GL_LIGHTING);
        glColor3f(1.0f, 0.55f, 0.35f);
        ball(0.0f, 0.045f, -0.42f, 0.05f * (muzzleFlashTimer / 0.08f), 8, 8);
        glEnable(GL_LIGHTING);
    }
    glPopMatrix();
}

/* Hitscan: a ray from the camera along the view direction, tested against
   a bounding sphere around each living zombie. One shot per 150 ms. */
static void tryShoot(void) {
    if (gameOver || gameWon)return;

    int now = glutGet(GLUT_ELAPSED_TIME);
    if (now - lastShotMs < 150)return;
    lastShotMs = now;
    shotsFired++;
    muzzleFlashTimer = 0.08f;

    float ry = yaw * PI / 180.0f, rp = pitch * PI / 180.0f;
    float dx = cosf(rp) * cosf(ry), dy = sinf(rp), dz = cosf(rp) * sinf(ry);
    float bestT = 60.0f;
    int hitIdx = -1;

    for (int i = 0; i < ZOMBIE_COUNT; i++) {
        const SEUZombieNPC* z = &seuZombies[i];
        if (z->state != 0)continue;
        float ocx = camX - z->x, ocy = camY - 1.0f, ocz = camZ - z->z, radius = 0.55f;
        float b = ocx * dx + ocy * dy + ocz * dz;
        float disc = b * b - (ocx * ocx + ocy * ocy + ocz * ocz - radius * radius);
        if (disc < 0.0f)continue;
        float t = -b - sqrtf(disc);
        if (t > 0.0f && t < bestT) { bestT = t; hitIdx = i; }
    }

    Tracer tr = { camX, camY, camZ, camX + dx * bestT, camY + dy * bestT, camZ + dz * bestT, 0.0f };
    tracers.push_back(tr);

    if (hitIdx >= 0 && --seuZombies[hitIdx].health <= 0) {
        seuZombies[hitIdx].state = 1;
        seuZombies[hitIdx].stateTimer = 0.0f;
        killCount++;
        if (killCount >= FLOOR_KILL_REQUIREMENT[currentFloor - 1]) {
            if (currentFloor < 3) { if (liftState == 0) liftState = 2; } /* quota met: lift opens */
            else gameWon = true;                                         /* final floor: win */
        }
    }
}

/* A zombie within arm's reach of the shooter is instant death. */
static void updateZombieContact(void) {
    if (gameOver || gameWon)return;
    for (int i = 0; i < ZOMBIE_COUNT; i++) {
        const SEUZombieNPC* z = &seuZombies[i];
        float dx = camX - z->x, dz = camZ - z->z;
        if (z->state == 0 && sqrtf(dx * dx + dz * dz) < 0.9f) {
            playerHealth = 0;
            gameOver = true;
            return;
        }
    }
}

/* Shared by a restart and a lift ride: every floor is laid out the same. */
static void startFloor(void) {
    killCount = 0;
    liftState = 0;
    liftDoorOpen = 0.0f;
    for (int i = 0; i < ZOMBIE_COUNT; i++) {
        seuZombies[i].health = 3;
        seuZombies[i].state = 0;
        seuZombies[i].stateTimer = 0.0f;
    }
    camX = 0.0f; camY = 1.7f; camZ = 10.0f;
    yaw = -90.0f; pitch = 0.0f;
}

static void resetGame(void) {
    startFloor();
    playerHealth = 100;
    shotsFired = 0;
    gameOver = gameWon = false;
    currentFloor = 1;
    floorMsgTimer = 0.0f;
    muzzleFlashTimer = 0.0f;
    tracers.clear();
}

static void advanceFloor(void) {
    if (currentFloor >= 3)return;
    currentFloor++;
    startFloor();
    floorMsgTimer = 2.5f;
}

/* Open lift -> close with the shooter inside -> ride to the next floor. */
static void useLift(void) {
    if (liftState == 2) liftState = 3;
    else if (liftState == 3) advanceFloor();
}

/* ------------------------------------------------------------ */
/* Haunted atmosphere: cobwebs, candles, scrawls, dust          */
/* ------------------------------------------------------------ */

/* Corner web hung between two walls and the ceiling. (cx,cy,cz) is the
   corner, dx/dz (+-1) point into the room along each wall, s is its size. */
static void drawCobweb(float cx, float cy, float cz, float dx, float dz, float s) {
    const float A[3] = { dx * s, 0, 0 }, B[3] = { 0, 0, dz * s }, D[3] = { 0, -s, 0 };
    const float* E[3][2] = { {A,B},{B,D},{D,A} };
    float spoke[9][3], m[3];
    int i, k;
    for (k = 0; k < 3; k++) m[k] = (A[k] + B[k] + D[k]) * .30f;
    for (i = 0; i < 9; i++) {
        float t = (i % 3) / 3.0f;
        for (k = 0; k < 3; k++) spoke[i][k] = E[i / 3][0][k] + (E[i / 3][1][k] - E[i / 3][0][k]) * t;
    }

    glPushMatrix();
    glTranslatef(cx, cy, cz);
    glColor4f(.78f, .78f, .74f, .32f);
    glBegin(GL_LINES);
    for (i = 0; i < 9; i++) { glVertex3fv(m); glVertex3fv(spoke[i]); }
    for (float f = .25f; f <= 1.01f; f += .25f)
        for (i = 0; i < 9; i++) {
            const float* p = spoke[i], * q = spoke[(i + 1) % 9];
            glVertex3f(m[0] + (p[0] - m[0]) * f, m[1] + (p[1] - m[1]) * f - .02f * f, m[2] + (p[2] - m[2]) * f);
            glVertex3f(m[0] + (q[0] - m[0]) * f, m[1] + (q[1] - m[1]) * f - .02f * f, m[2] + (q[2] - m[2]) * f);
        }
    glVertex3fv(m); glVertex3f(m[0] + .05f, m[1] - s * .7f, m[2] + .05f); /* loose strand */
    glEnd();
    glPopMatrix();
}

static void drawCobwebs(void) {
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.0f);
    for (int sx = -1; sx <= 1; sx += 2) { /* corridor ends */
        drawCobweb(23.64f * sx, 3.1f, 18.16f, (float)-sx, 1, 1.2f);
        drawCobweb(23.64f * sx, 3.1f, 27.84f, (float)-sx, -1, 1.0f);
    }
    for (int i = 0; i < 4; i++)for (int side = 0; side < 2; side++)for (int sx = -1; sx <= 1; sx += 2) {
        float h = hash2(i * 4 + side, sx + 3);
        if (h < .3f) continue;
        drawCobweb(ROOM_X[i] + 3.69f * sx, 3.1f, side ? 38.01f : 7.84f, (float)-sx, side ? -1.0f : 1.0f, .55f + h * .6f);
    }
    drawCobweb(7.34f, 3.1f, 48.54f, -1, -1, .9f); /* washroom */
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

#define CANDLE_COUNT 10
static const float CANDLES[CANDLE_COUNT][3] = {
    {-20.0f,.06f,18.45f},{-11.0f,.06f,18.45f},{-8.4f,.06f,27.55f},{1.5f,.06f,18.45f},
    {9.0f,.06f,27.55f},{19.5f,.06f,18.45f},{-2.2f,.06f,40.60f},{6.95f,.90f,43.72f},
    {-18.5f,1.15f,9.30f},{13.4f,1.15f,36.20f} };

static float candleFlicker(int i) {
    return .82f + .10f * sinf(hauntTime * 23.0f + i * 3.1f) + .08f * sinf(hauntTime * 37.0f + i);
}

static void drawCandleBodies(void) {
    for (int i = 0; i < CANDLE_COUNT; i++) {
        float x = CANDLES[i][0], y = CANDLES[i][1], z = CANDLES[i][2];
        setColor(.80f, .76f, .64f);
        box(x, y + .005f, z, .22f, .012f, .16f);           /* wax puddle */
        box(x, y + .11f, z, .08f, .22f, .08f);             /* main candle */
        box(x + .09f, y + .07f, z + .03f, .06f, .14f, .06f); /* stubby one */
        setColor(.05f, .05f, .05f);
        box(x, y + .235f, z, .01f, .03f, .01f);             /* wick */
    }
}

/* Flames and glow halos, additive, drawn after the opaque scene. */
static void drawCandleFlames(void) {
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    for (int i = 0; i < CANDLE_COUNT; i++) {
        float x = CANDLES[i][0], y = CANDLES[i][1] + .29f, z = CANDLES[i][2], f = candleFlicker(i);
        float sway = .008f * sinf(hauntTime * 6.0f + i);
        glColor4f(1.0f, .45f, .10f, .9f);  ball(x + sway, y, z, .028f, 8, 6, 1, 2.1f * f, 1);
        glColor4f(1.0f, .88f, .55f, .9f);  ball(x + sway, y - .01f, z, .014f, 8, 6, 1, 2.0f * f, 1);
        glColor4f(1.0f, .50f, .15f, .07f * f); ball(x, y, z, .16f, 12, 8);
        glColor4f(1.0f, .50f, .15f, .03f * f); ball(x, y, z, .32f, 12, 8);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

static int nearestCandle(void) {
    int best = 0;
    float bd = 1e9f;
    for (int i = 0; i < CANDLE_COUNT; i++) {
        float dx = camX - CANDLES[i][0], dz = camZ - CANDLES[i][2], d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* Dripping red words painted on a wall with the GLUT stroke font. */
static void drawWallScrawl(const char* msg, float x, float y, float z, float rotY, float scale) {
    float w = 0;
    for (const char* p = msg; *p; p++) w += glutStrokeWidth(GLUT_STROKE_ROMAN, *p);
    float W = w * scale;

    glDisable(GL_LIGHTING);
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(rotY, 0, 1, 0);
    glColor3f(.36f, .02f, .02f);
    glLineWidth(3.0f);
    glBegin(GL_LINES); /* drips */
    for (int i = 0; i < 14; i++) {
        float dx = -W / 2 + hash2(i, (int)(x * 10)) * W, len = .08f + hash2((int)(x * 10), i) * .45f;
        glVertex3f(dx, .06f, 0); glVertex3f(dx, -len, 0);
    }
    glEnd();
    glLineWidth(5.0f);
    glTranslatef(-W / 2, 0, 0);
    glScalef(scale, scale, scale);
    for (const char* p = msg; *p; p++) glutStrokeCharacter(GLUT_STROKE_ROMAN, *p);
    glPopMatrix();
    glLineWidth(1.0f);
    glEnable(GL_LIGHTING);
}

/* Dust motes hanging around the shooter; they wrap to stay within 7 m. */
#define DUST_COUNT 180
static float dust[DUST_COUNT][4]; /* x, y, z, phase */

static void initDust(void) {
    for (int i = 0; i < DUST_COUNT; i++) {
        dust[i][0] = camX + (frand() * 2 - 1) * 7;
        dust[i][1] = .2f + frand() * 2.9f;
        dust[i][2] = camZ + (frand() * 2 - 1) * 7;
        dust[i][3] = frand() * 2 * PI;
    }
}

static void updateDust(void) {
    for (int i = 0; i < DUST_COUNT; i++) {
        float* d = dust[i];
        d[0] += .002f * sinf(hauntTime * .3f + d[3]);
        d[1] += .0015f * sinf(hauntTime * .5f + d[3] * 2) - .0004f;
        d[2] += .002f * cosf(hauntTime * .35f + d[3]);
        if (d[1] < .1f) d[1] = 3.0f;
        if (d[0] - camX > 7) d[0] -= 14; else if (d[0] - camX < -7) d[0] += 14;
        if (d[2] - camZ > 7) d[2] -= 14; else if (d[2] - camZ < -7) d[2] += 14;
    }
}

static void drawDust(void) {
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPointSize(2.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < DUST_COUNT; i++) {
        glColor4f(.80f, .82f, .75f, .20f + .20f * sinf(hauntTime * 2.0f + dust[i][3]));
        glVertex3f(dust[i][0], dust[i][1], dust[i][2]);
    }
    glEnd();
    glPointSize(1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* Screen-space "old film" pass: cold tint, vignette, scanlines, grain,
   exposure flicker and the odd vertical scratch. Expects the 2D HUD
   projection with lighting off. */
static void drawOldFilmOverlay(void) {
    const float cx = WIN_W / 2.0f, cy = WIN_H / 2.0f;
    int i;
    glEnable(GL_BLEND);

    glBlendFunc(GL_DST_COLOR, GL_ZERO); /* multiply */
    glColor3f(.90f, .96f, .92f);
    glRectf(0, 0, WIN_W, WIN_H);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= 48; i++) {
        float a = i * 2 * PI / 48, c = cosf(a), s = sinf(a);
        glColor4f(0, 0, 0, 0);    glVertex2f(cx + c * WIN_W * .30f, cy + s * WIN_H * .30f);
        glColor4f(0, 0, 0, .65f); glVertex2f(cx + c * WIN_W * .75f, cy + s * WIN_H * .75f);
    }
    glEnd();

    glColor4f(0, 0, 0, .14f);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (i = 0; i < WIN_H; i += 3) { glVertex2f(0, (float)i); glVertex2f(WIN_W, (float)i); }
    glEnd();

    glPointSize(1.5f);
    glBegin(GL_POINTS);
    for (i = 0; i < 700; i++) {
        float g = frand();
        glColor4f(g, g, g, .10f);
        glVertex2f(frand() * WIN_W, frand() * WIN_H);
    }
    glEnd();
    glPointSize(1.0f);

    glColor4f(0, 0, 0, rand() % 90 == 0 ? .15f : .02f * frand());
    glRectf(0, 0, WIN_W, WIN_H);

    if (rand() % 6 == 0) {
        float x = frand() * WIN_W;
        glColor4f(.85f, .85f, .80f, .16f);
        glBegin(GL_LINES);
        glVertex2f(x, 0); glVertex2f(x + (frand() - .5f) * 8, WIN_H);
        glEnd();
    }
    glDisable(GL_BLEND);
}

/* ------------------------------------------------------------ */
/* Scene                                                        */
/* ------------------------------------------------------------ */

static void drawScene(void) {
    drawBuildingShell();
    drawWallList();

    for (int i = 0; i < 8; i++) drawClassroom(ROOM_X[i % 4], i < 4 ? 12.5f : 33.0f);
    drawWashroom(2.5f, 45.0f);

    for (int i = 0; i < doorCount; i++) {
        if (doors[i].type == 0 || doors[i].type == 2) drawWoodDoor(&doors[i]);
        else if (doors[i].type == 3) drawWashroomCubicleDoor(&doors[i]);
    }
    drawElevator(-6.65f, 41.0f, liftDoorOpen, 1);
    drawElevator(-4.85f, 41.0f, 0.0f, 0);

    drawWindowView(23.55f, 1.9f, 23.0f, -90.0f); /* east wall, between the classroom rows */
    drawCandleBodies();
    drawWallScrawl("GET OUT", -11.0f, 1.45f, 18.18f, 0.0f, .0055f);
    drawWallScrawl("HELP ME", 11.0f, 1.55f, 27.82f, 180.0f, .0050f);
    drawWallScrawl("IT FOLLOWS", -.8f, 1.60f, 40.83f, 180.0f, .0038f);
    drawSEUZombies();
}

/* Translucent pieces, after everything opaque. */
static void drawHauntedEffects(void) {
    drawCobwebs();
    drawCandleFlames();
    drawDust();
}

/* ------------------------------------------------------------ */
/* Doors / lift interaction                                      */
/* ------------------------------------------------------------ */

static void updateNearestDoor(void) {
    float best = 3.4f;
    nearestDoor = -1;
    for (int i = 0; i < doorCount; i++) {
        float dx = camX - doors[i].x, dz = camZ - doors[i].z, d = sqrtf(dx * dx + dz * dz);
        if (d < best) { best = d; nearestDoor = i; }
    }
}

/* Toggle the nearest door, or use the lift. Inside the washroom a nearby
   cubicle door takes priority over the main washroom door. */
static void interact(void) {
    updateNearestDoor();
    int hit = nearestDoor;
    float best = 3.0f;
    for (int i = 0; i < doorCount; i++) {
        if (doors[i].type != 3)continue;
        float dx = camX - doors[i].x, dz = camZ - doors[i].z, d = sqrtf(dx * dx + dz * dz);
        if (d < best) { best = d; hit = i; }
    }
    if (hit < 0)return;
    if (hit == LIFT_DOOR_INDEX) {
        if (currentFloor < 3) useLift();
        return;
    }
    nearestDoor = hit;
    doorTarget[hit] = !doorTarget[hit];
}

/* Hinged doors swing toward 78 degrees (cubicles -78) or 0; the lift's
   sliding panels move toward open only while liftState == 2. */
static void animateDoors(void) {
    for (int i = 0; i < doorCount; i++) {
        if (doors[i].type == 1)continue;
        float target = doorTarget[i] ? (doors[i].type == 3 ? -78.0f : 78.0f) : 0.0f;
        float delta = target - doors[i].angle;
        if (fabsf(delta) < .7f) doors[i].angle = target;
        else doors[i].angle += delta * .18f;
    }

    float target = (liftState == 2) ? 1.0f : 0.0f, delta = target - liftDoorOpen;
    if (fabsf(delta) < .02f) liftDoorOpen = target;
    else liftDoorOpen += delta * .12f;
    if (floorMsgTimer > 0.0f) floorMsgTimer -= 0.016f;
}

/* ------------------------------------------------------------ */
/* Input / movement                                              */
/* ------------------------------------------------------------ */

static void movePlayer(void) {
    float s = (keyState['j'] == 2) ? 0.13f : 0.06f;
    float r = yaw * PI / 180.0f;
    float fx = cosf(r), fz = sinf(r);
    float rx = cosf(r + PI / 2), rz = sinf(r + PI / 2);
    float mx = 0, mz = 0;

    if (keyState['w'] || specialState[GLUT_KEY_UP]) { mx += fx; mz += fz; }
    if (keyState['s'] || specialState[GLUT_KEY_DOWN]) { mx -= fx; mz -= fz; }
    if (keyState['a'] || specialState[GLUT_KEY_LEFT]) { mx -= rx; mz -= rz; }
    if (keyState['d'] || specialState[GLUT_KEY_RIGHT]) { mx += rx; mz += rz; }

    float len = sqrtf(mx * mx + mz * mz);
    if (len > .0001f) {
        float nx = camX + mx / len * s, nz = camZ + mz / len * s;
        if (!collides(nx, camZ))camX = nx;
        if (!collides(camX, nz))camZ = nz;
    }
    camY = 1.7f;
}

/* Left button fires (hold for automatic fire); right button uses doors/lift. */
static void mouseButton(int button, int state, int x, int y) {
    (void)x; (void)y;
    if (button == GLUT_LEFT_BUTTON) {
        mouseFire = (state == GLUT_DOWN);
        if (mouseFire) tryShoot();
    }
    else if (button == GLUT_RIGHT_BUTTON && state == GLUT_DOWN) interact();
}

static void keyboard(unsigned char key, int x, int y) {
    (void)x; (void)y;
    key = (unsigned char)tolower(key);
    if (key == 27)exit(0);
    if ((gameOver || gameWon) && key == 'r') { resetGame(); return; }
    if (key == 'e') { interact(); return; }
    if (key == 'o' && !gameOver && !gameWon) { useLift(); return; }
    keyState[key] = 1;
}

static void keyboardUp(unsigned char key, int x, int y) {
    (void)x; (void)y;
    keyState[(unsigned char)tolower(key)] = 0;
}

static void specialDown(int key, int x, int y) { (void)x; (void)y; if (key < 256)specialState[key] = 1; }
static void specialUp(int key, int x, int y) { (void)x; (void)y; if (key < 256)specialState[key] = 0; }

/* Used for both passive and button-held motion, so aiming works while firing. */
static void mouseMotion(int x, int y) {
    if (warpingPointer) { warpingPointer = 0; return; }
    int cx = WIN_W / 2, cy = WIN_H / 2;
    yaw += (x - cx) * .12f;
    pitch -= (y - cy) * .12f;
    if (pitch > 89)pitch = 89;
    if (pitch < -89)pitch = -89;
    warpingPointer = 1;
    glutWarpPointer(cx, cy);
}

/* ------------------------------------------------------------ */
/* Render                                                       */
/* ------------------------------------------------------------ */

/* Compass arrow pointing at Elevator A (floors 1 and 2 only). */
static void drawLiftCompass(void) {
    if (currentFloor >= 3) return;

    float dx = -6.65f - camX, dz = 41.0f - camZ, dist = sqrtf(dx * dx + dz * dz);
    float rel = atan2f(dz, dx) * 180.0f / PI - yaw;
    while (rel > 180.0f) rel -= 360.0f;
    while (rel < -180.0f) rel += 360.0f;
    float rad = rel * PI / 180.0f, cx = WIN_W / 2.0f, cy = 92.0f, radius = 28.0f;

    glColor3f(.5f, .5f, .5f);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 32; i++) {
        float a = i * 2.0f * PI / 32;
        glVertex2f(cx + sinf(a) * radius, cy + cosf(a) * radius);
    }
    glEnd();

    float ax = cx + sinf(rad) * radius, ay = cy + cosf(rad) * radius;
    glColor3f(.55f, 1.0f, .65f);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    glVertex2f(cx, cy);
    glVertex2f(ax, ay);
    glEnd();
    glBegin(GL_TRIANGLES);
    glVertex2f(ax, ay);
    glVertex2f(ax - sinf(rad + 2.6f) * 8.0f, ay - cosf(rad + 2.6f) * 8.0f);
    glVertex2f(ax - sinf(rad - 2.6f) * 8.0f, ay - cosf(rad - 2.6f) * 8.0f);
    glEnd();

    glColor3f(1, 1, 1);
    drawText(cx - 26, cy + 40, "Lift %.0fm", dist);
}

static void drawHUD(void) {
    const float mx = WIN_W / 2.0f, my = WIN_H / 2.0f;

    glColor3f(.62f, .80f, .66f);
    drawText(14, WIN_H - 25, "Corridor of the Dead - Haunted Night | WASD/Arrows | Shift Run | "
        "Left Click/Space Shoot | Right Click/E Door | ESC Quit");
    glColor3f(.88f, .82f, .62f);
    drawText(14, WIN_H - 50, "Floor: %d/3   Health: %d   Kills: %d/%d   Shots: %d",
        currentFloor, playerHealth, killCount, FLOOR_KILL_REQUIREMENT[currentFloor - 1], shotsFired);

    if (nearestDoor >= 0) {
        const Door* d = &doors[nearestDoor];
        if (nearestDoor == LIFT_DOOR_INDEX && currentFloor < 3) {
            int left = FLOOR_KILL_REQUIREMENT[currentFloor - 1] - killCount;
            if (liftState == 0)
                drawText(mx - 210, 80, "Elevator - LOCKED (kill %d more zombie%s)", left, left == 1 ? "" : "s");
            else if (liftState == 2) drawText(mx - 210, 80, "Elevator - RIGHT CLICK / E TO CLOSE");
            else drawText(mx - 210, 80, "Elevator - RIGHT CLICK / E TO GO TO FLOOR %d", currentFloor + 1);
        }
        else if (d->type == 1) drawText(mx - 210, 80, "%s - LIFT CLOSED / ENTRY BLOCKED", d->label);
        else drawText(mx - 210, 80, "%s - RIGHT CLICK / E TO %s", d->label, doorTarget[nearestDoor] ? "CLOSE" : "OPEN");
    }

    if (floorMsgTimer > 0.0f) {
        glColor3f(.6f, .9f, 1.0f);
        drawText(mx - 40, my + 90, "FLOOR %d", currentFloor);
    }

    glColor3f(1, 1, 1); /* crosshair */
    glBegin(GL_LINES);
    glVertex2f(mx - 8, my); glVertex2f(mx + 8, my);
    glVertex2f(mx, my - 8); glVertex2f(mx, my + 8);
    glEnd();

    if (!gameOver && !gameWon) drawLiftCompass();
    if (gameOver) {
        glColor3f(1.0f, 0.2f, 0.2f); drawText(mx - 55, my + 40, "YOU DIED");
        glColor3f(1, 1, 1);          drawText(mx - 95, my + 15, "Press R to restart");
    }
    if (gameWon) {
        glColor3f(0.25f, 1.0f, 0.35f); drawText(mx - 75, my + 40, "YOU WIN!");
        glColor3f(1, 1, 1);            drawText(mx - 165, my + 15, "All 3 floors cleared - Press R to play again");
    }
}

static void display(void) {
    glClearColor(.12f, .13f, .14f, 1); /* matches the fog */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(70.0, (double)WIN_W / WIN_H, .1, 200);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float ry = yaw * PI / 180.0f, rp = pitch * PI / 180.0f;
    float lx = cosf(rp) * cosf(ry), ly = sinf(rp), lz = cosf(rp) * sinf(ry);
    gluLookAt(camX, camY, camZ, camX + lx, camY + ly, camZ + lz, 0, 1, 0);

    /* Two point lights plus a flashlight spot that follows the camera. */
    GLfloat lp0[] = { 7,18,20,1 }, lp1[] = { -4,17,45,1 }, lp2[] = { camX,camY,camZ,1 }, spotDir[] = { lx,ly,lz };
    glLightfv(GL_LIGHT0, GL_POSITION, lp0);
    glLightfv(GL_LIGHT1, GL_POSITION, lp1);
    glLightfv(GL_LIGHT2, GL_POSITION, lp2);
    glLightfv(GL_LIGHT2, GL_SPOT_DIRECTION, spotDir);

    /* The building light flickers with the tubes; the nearest candle adds a
       warm, wavering point light. */
    float f = fmaxf(lampGlow(3), .6f);
    GLfloat d0[] = { .78f * f,.80f * f,.82f * f,1 };
    glLightfv(GL_LIGHT0, GL_DIFFUSE, d0);
    int nc = nearestCandle();
    float cf = candleFlicker(nc);
    GLfloat lp3[] = { CANDLES[nc][0],CANDLES[nc][1] + .3f,CANDLES[nc][2],1 }, d3[] = { 1.0f * cf,.52f * cf,.20f * cf,1 };
    glLightfv(GL_LIGHT3, GL_POSITION, lp3);
    glLightfv(GL_LIGHT3, GL_DIFFUSE, d3);
    glFogf(GL_FOG_DENSITY, .045f + .010f * sinf(hauntTime * .25f)); /* the mist breathes */

    glEnable(GL_FOG);
    drawScene();
    drawHauntedEffects();
    updateNearestDoor();

    /* Bullet tracers. */
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.5f);
    glBegin(GL_LINES);
    for (size_t i = 0; i < tracers.size(); ++i) {
        const Tracer& t = tracers[i];
        glColor4f(1.0f, 0.95f, 0.55f, 1.0f - t.age / TRACER_LIFE);
        glVertex3f(t.x1, t.y1, t.z1);
        glVertex3f(t.x2, t.y2, t.z2);
    }
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glDisable(GL_FOG);

    /* First-person gun, drawn over the scene; extra ambient so it doesn't
       vanish into the dark. */
    glClear(GL_DEPTH_BUFFER_BIT);
    if (!gameOver && !gameWon) {
        bool moving = keyState['w'] || keyState['a'] || keyState['s'] || keyState['d'];
        GLfloat gunAmb[] = { .34f,.32f,.30f,1 }, sceneAmb[] = { .20f,.20f,.22f,1 };
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, gunAmb);
        glPushMatrix();
        glLoadIdentity();
        drawViewmodelGun(moving ? sinf(gunBobPhase * 2.0f) * 0.015f : 0.0f,
            moving ? fabsf(sinf(gunBobPhase * 2.0f)) * 0.02f : 0.0f);
        glPopMatrix();
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, sceneAmb);
    }

    /* 2D HUD. */
    glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, WIN_W, 0, WIN_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST); /* full-screen overlay layers must not occlude the HUD */
    drawOldFilmOverlay();
    drawHUD();
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glEnable(GL_LIGHTING);

    glutSwapBuffers();
}

static void timer(int v) {
    (void)v;
    keyState['j'] = (glutGetModifiers() & GLUT_ACTIVE_SHIFT) ? 2 : 0;
    hauntTime += 0.016f;
    updateDust();

    fanAngle += 2.5f; /* slow, creaky fans */
    if (fanAngle > 360.0f) fanAngle -= 360.0f;
    animateDoors();

    if (!gameOver && !gameWon) {
        movePlayer();
        zombieAnimTime += 0.016f;
        updateSEUZombies();
        updateZombieContact();
        if (keyState[' '] || mouseFire) tryShoot();

        bool moving = keyState['w'] || keyState['a'] || keyState['s'] || keyState['d'];
        if (moving) gunBobPhase += 0.09f; else gunBobPhase *= 0.85f;
        if (muzzleFlashTimer > 0.0f) muzzleFlashTimer -= 0.016f;

        for (size_t i = 0; i < tracers.size();) {
            tracers[i].age += 0.016f;
            if (tracers[i].age > TRACER_LIFE) tracers.erase(tracers.begin() + i);
            else ++i;
        }
    }

    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

/* ------------------------------------------------------------ */
/* Init / main                                                   */
/* ------------------------------------------------------------ */

static void setupLight(GLenum light, const GLfloat* diff, const GLfloat* amb, const GLfloat* spec) {
    glEnable(light);
    glLightfv(light, GL_DIFFUSE, diff);
    glLightfv(light, GL_AMBIENT, amb);
    glLightfv(light, GL_SPECULAR, spec);
}

static void initGL(void) {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);
    glEnable(GL_LIGHTING);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);

    GLfloat globalAmb[] = { .20f,.20f,.22f,1 };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, globalAmb);

    /* LIGHT0 and LIGHT1: dim, cold point lights (LIGHT0 flickers).
       LIGHT2: flashlight spot on the gun. LIGHT3: nearest candle. */
    GLfloat diff0[] = { .78f,.80f,.82f,1 }, amb0[] = { .22f,.22f,.24f,1 }, spec0[] = { .2f,.2f,.2f,1 };
    GLfloat diff1[] = { .32f,.44f,.40f,1 }, amb1[] = { .06f,.07f,.07f,1 }, spec1[] = { .1f,.1f,.1f,1 };
    GLfloat diff2[] = { 1.0f,.92f,.70f,1 }, amb2[] = { .03f,.03f,.02f,1 }, spec2[] = { .6f,.6f,.5f,1 };
    GLfloat diff3[] = { 1.0f,.52f,.20f,1 }, amb3[] = { 0,0,0,1 }, spec3[] = { 0,0,0,1 };
    setupLight(GL_LIGHT0, diff0, amb0, spec0);
    setupLight(GL_LIGHT1, diff1, amb1, spec1);
    setupLight(GL_LIGHT2, diff2, amb2, spec2);
    setupLight(GL_LIGHT3, diff3, amb3, spec3);
    glLightf(GL_LIGHT3, GL_CONSTANT_ATTENUATION, 1.0f);
    glLightf(GL_LIGHT3, GL_LINEAR_ATTENUATION, .35f);
    glLightf(GL_LIGHT3, GL_QUADRATIC_ATTENUATION, .30f);

    /* Thick exponential mist; switched on only for the 3D pass. */
    GLfloat fogCol[] = { .12f,.13f,.14f,1 };
    glFogi(GL_FOG_MODE, GL_EXP2);
    glFogfv(GL_FOG_COLOR, fogCol);
    glFogf(GL_FOG_DENSITY, .045f);
    glHint(GL_FOG_HINT, GL_NICEST);
    glLightf(GL_LIGHT2, GL_SPOT_CUTOFF, 22.0f);
    glLightf(GL_LIGHT2, GL_SPOT_EXPONENT, 18.0f);
    glLightf(GL_LIGHT2, GL_CONSTANT_ATTENUATION, 1.0f);
    glLightf(GL_LIGHT2, GL_LINEAR_ATTENUATION, 0.03f);
    glLightf(GL_LIGHT2, GL_QUADRATIC_ATTENUATION, 0.006f);

    genTextures();
    buildBuildingGeometry();
    initDust();

    zombieQuad = gluNewQuadric();
    gluQuadricNormals(zombieQuad, GLU_SMOOTH);
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(WIN_W, WIN_H);
    glutCreateWindow("SEU Corridor Explorer v13 - Haunted Night");

    initGL();

    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutKeyboardUpFunc(keyboardUp);
    glutSpecialFunc(specialDown);
    glutSpecialUpFunc(specialUp);
    glutMouseFunc(mouseButton);
    glutMotionFunc(mouseMotion);
    glutPassiveMotionFunc(mouseMotion);
    glutSetCursor(GLUT_CURSOR_NONE);
    glutWarpPointer(WIN_W / 2, WIN_H / 2);
    glutTimerFunc(16, timer, 0);

    glutMainLoop();
    return 0;
}