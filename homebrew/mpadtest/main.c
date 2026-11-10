// 8-controller input viewer for the multi-pad melonDS build.
// Bare metal (no libnds): reads the MPAD block at 0x04FFF000 and draws
// controllers 1-4 on the top screen and 5-8 on the bottom screen.

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed int s32;

#define REG32(a) (*(volatile u32*)(a))
#define REG16(a) (*(volatile u16*)(a))
#define REG8(a)  (*(volatile u8*)(a))

#define DISPCNT_A   REG32(0x04000000)
#define VCOUNT      REG16(0x04000006)
#define DISPCNT_B   REG32(0x04001000)
#define BG3CNT_B    REG16(0x0400100E)
#define BG3PA_B     REG16(0x04001030)
#define BG3PB_B     REG16(0x04001032)
#define BG3PC_B     REG16(0x04001034)
#define BG3PD_B     REG16(0x04001036)
#define BG3X_B      REG32(0x04001038)
#define BG3Y_B      REG32(0x0400103C)
#define VRAMCNT_A   REG8(0x04000240)
#define VRAMCNT_C   REG8(0x04000242)
#define POWCNT1     REG16(0x04000304)

// multi-pad block (see NDS.h in the melonDS fork)
#define MPAD_BASE   0x04FFF000
#define MPAD_MAGIC  0x4441504D  // "MPAD"
#define MPAD_PAD(n) (MPAD_BASE + 0x10 + (n) * 0x10)

// button bits in the per-pad word
#define K_A      (1u << 0)
#define K_B      (1u << 1)
#define K_SELECT (1u << 2)
#define K_START  (1u << 3)
#define K_RIGHT  (1u << 4)
#define K_LEFT   (1u << 5)
#define K_UP     (1u << 6)
#define K_DOWN   (1u << 7)
#define K_R      (1u << 8)
#define K_L      (1u << 9)
#define K_L2     (1u << 10)
#define K_R2     (1u << 11)
#define K_L3     (1u << 12)
#define K_R3     (1u << 13)
#define K_X      (1u << 16)
#define K_Y      (1u << 17)
#define K_LS_RIGHT (1u << 24)
#define K_LS_LEFT  (1u << 25)
#define K_LS_UP    (1u << 26)
#define K_LS_DOWN  (1u << 27)
#define K_RS_RIGHT (1u << 28)
#define K_RS_LEFT  (1u << 29)
#define K_RS_UP    (1u << 30)
#define K_RS_DOWN  (1u << 31)

#define RGB(r,g,b) ((u16)((r) | ((g) << 5) | ((b) << 10) | 0x8000))

static const u16 C_BG      = RGB(2, 2, 3);
static const u16 C_PANEL   = RGB(4, 4, 6);
static const u16 C_BORDER  = RGB(10, 10, 14);
static const u16 C_OFF     = RGB(8, 8, 11);
static const u16 C_ON      = RGB(31, 18, 2);
static const u16 C_TEXT    = RGB(26, 26, 28);
static const u16 C_DIM     = RGB(12, 12, 14);
static const u16 C_RING    = RGB(14, 14, 18);
static const u16 C_DOT     = RGB(31, 31, 31);
static const u16 C_PRESSED = RGB(20, 11, 1);
static const u16 C_GOOD    = RGB(6, 26, 8);
static const u16 C_BAD     = RGB(28, 6, 6);

void* memset(void* d, int c, unsigned n)
{
    u8* p = (u8*)d;
    while (n--) *p++ = (u8)c;
    return d;
}

int memcmp(const void* a, const void* b, unsigned n)
{
    const u8* p = (const u8*)a;
    const u8* q = (const u8*)b;
    for (; n; n--, p++, q++)
        if (*p != *q) return *p - *q;
    return 0;
}

void* memcpy(void* d, const void* s, unsigned n)
{
    u8* p = (u8*)d;
    const u8* q = (const u8*)s;
    while (n--) *p++ = *q++;
    return d;
}

static u16* fb; // current target framebuffer (256 px stride)

// drawing goes to RAM back buffers, copied to VRAM during vblank
static u16 backbuf[2][256 * 192] __attribute__((aligned(32)));
static int dirty[2];
#define VRAM_TOP ((u16*)0x06800000)
#define VRAM_BOT ((u16*)0x06200000)

static void copy_screen(u16* dst, const u16* src)
{
    u32* d = (u32*)dst;
    const u32* s = (const u32*)src;
    for (int i = 0; i < 256 * 192 / 2; i += 8)
    {
        d[i+0] = s[i+0]; d[i+1] = s[i+1]; d[i+2] = s[i+2]; d[i+3] = s[i+3];
        d[i+4] = s[i+4]; d[i+5] = s[i+5]; d[i+6] = s[i+6]; d[i+7] = s[i+7];
    }
}

static void pset(int x, int y, u16 c)
{
    if ((unsigned)x < 256 && (unsigned)y < 192) fb[y * 256 + x] = c;
}

static void rect(int x, int y, int w, int h, u16 c)
{
    for (int j = 0; j < h; j++)
    {
        int yy = y + j;
        if ((unsigned)yy >= 192) continue;
        u16* row = fb + yy * 256;
        for (int i = 0; i < w; i++)
        {
            int xx = x + i;
            if ((unsigned)xx < 256) row[xx] = c;
        }
    }
}

static void frame(int x, int y, int w, int h, u16 c)
{
    rect(x, y, w, 1, c);
    rect(x, y + h - 1, w, 1, c);
    rect(x, y, 1, h, c);
    rect(x + w - 1, y, 1, h, c);
}

static void disc(int cx, int cy, int r, u16 c)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r + r)
                pset(cx + dx, cy + dy, c);
}

static void ring(int cx, int cy, int r, u16 c)
{
    int lo = (r - 1) * (r - 1), hi = r * r + r;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
        {
            int d = dx * dx + dy * dy;
            if (d > lo && d <= hi) pset(cx + dx, cy + dy, c);
        }
}

// 3x5 font, one octal digit per row (bit 2 = left column)
static const char* glyph(char ch)
{
    static const char* digits[10] = {"75557","26227","71747","71717","55711","74717","74757","71222","75757","75711"};
    static const char* letters[26] = {
        "25755","65656","34443","65556","74647","74644","34553","55755","72227","11152",
        "55655","44447","57755","65555","25552","65644","25563","65655","34216","72222",
        "55557","55552","55775","55255","55222","71247"};
    if (ch >= '0' && ch <= '9') return digits[ch - '0'];
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch >= 'A' && ch <= 'Z') return letters[ch - 'A'];
    switch (ch)
    {
    case '+': return "02720";
    case '-': return "00700";
    case ':': return "02020";
    case '.': return "00002";
    }
    return "00000";
}

static void text(int x, int y, const char* s, u16 c)
{
    for (; *s; s++, x += 4)
    {
        const char* g = glyph(*s);
        for (int row = 0; row < 5; row++)
        {
            int bits = g[row] - '0';
            for (int col = 0; col < 3; col++)
                if (bits & (4 >> col)) pset(x + col, y + row, c);
        }
    }
}

static void hex32(char* out, u32 v)
{
    for (int i = 0; i < 8; i++)
    {
        int n = (v >> (28 - 4 * i)) & 0xF;
        out[i] = n < 10 ? '0' + n : 'A' + n - 10;
    }
    out[8] = 0;
}

static void sdec(char* out, int v)
{
    // "+127" / "-128" / "+000"
    out[0] = v < 0 ? '-' : '+';
    if (v < 0) v = -v;
    out[1] = '0' + (v / 100) % 10;
    out[2] = '0' + (v / 10) % 10;
    out[3] = '0' + v % 10;
    out[4] = 0;
}

typedef struct
{
    u32 buttons;
    s8 axes[4];
    u8 trig[2];
    int connected;
} PadState;

static void read_pad(int n, PadState* p)
{
    memset(p, 0, sizeof(*p));
    u32 conn = REG32(MPAD_BASE + 4) >> 24;
    p->buttons = REG32(MPAD_PAD(n));
    u32 a = REG32(MPAD_PAD(n) + 4);
    u32 t = REG32(MPAD_PAD(n) + 8);
    p->axes[0] = (s8)(a & 0xFF);
    p->axes[1] = (s8)((a >> 8) & 0xFF);
    p->axes[2] = (s8)((a >> 16) & 0xFF);
    p->axes[3] = (s8)(a >> 24);
    p->trig[0] = t & 0xFF;
    p->trig[1] = (t >> 8) & 0xFF;
    p->connected = (conn >> n) & 1;
}

static void select_pad_fb(int n, int* ox, int* oy)
{
    fb = backbuf[n < 4 ? 0 : 1];
    dirty[n < 4 ? 0 : 1] = 1;
    int k = n & 3;
    *ox = (k & 1) * 128;
    *oy = (k >> 1) * 96;
}

static void draw_panel_static(int n, int connected)
{
    int ox, oy;
    select_pad_fb(n, &ox, &oy);
    rect(ox, oy, 128, 96, C_BG);
    rect(ox + 1, oy + 1, 126, 94, C_PANEL);
    frame(ox + 1, oy + 1, 126, 94, connected ? C_BORDER : C_OFF);

    char t[3] = {'P', (char)('1' + n), 0};
    text(ox + 5, oy + 4, t, connected ? C_TEXT : C_DIM);
    text(ox + 15, oy + 4, connected ? "ON" : "--", connected ? C_GOOD : C_DIM);
}

static void button_box(int x, int y, int w, int h, int on, const char* label, u16 tc)
{
    rect(x, y, w, h, on ? C_ON : C_OFF);
    if (label)
    {
        int len = 0;
        while (label[len]) len++;
        text(x + (w - len * 4 + 1) / 2, y + (h - 5) / 2, label, on ? C_BG : tc);
    }
}

static void trigger_bar(int x, int y, int w, int h, int val, int on, const char* label, u16 tc)
{
    rect(x, y, w, h, C_OFF);
    int fill = (w * val) >> 8;
    if (on && fill < 2) fill = 2;
    rect(x, y, fill, h, on ? C_ON : RGB(20, 12, 2));
    text(x + 2, y + (h - 5) / 2, label, on ? C_BG : tc);
}

static void face(int cx, int cy, int on, const char* label, u16 tc)
{
    disc(cx, cy, 4, on ? C_ON : C_OFF);
    text(cx - 1, cy - 2, label, on ? C_BG : tc);
}

static void stick(int cx, int cy, int ax, int ay, int pressed, u32 dirs, u32 bR, u32 bL, u32 bU, u32 bD, const char* name, u16 tc)
{
    rect(cx - 17, cy - 17, 35, 35, C_PANEL);
    disc(cx, cy, 13, pressed ? C_PRESSED : RGB(5, 5, 7));
    ring(cx, cy, 14, C_RING);
    // digital direction marks
    rect(cx + 15, cy - 1, 2, 3, (dirs & bR) ? C_ON : C_OFF);
    rect(cx - 16, cy - 1, 2, 3, (dirs & bL) ? C_ON : C_OFF);
    rect(cx - 1, cy - 16, 3, 2, (dirs & bU) ? C_ON : C_OFF);
    rect(cx - 1, cy + 15, 3, 2, (dirs & bD) ? C_ON : C_OFF);
    // cross hair
    rect(cx - 10, cy, 21, 1, RGB(8, 8, 10));
    rect(cx, cy - 10, 1, 21, RGB(8, 8, 10));
    int dx = (ax * 10) >> 7;
    int dy = (ay * 10) >> 7;
    disc(cx + dx, cy + dy, 3, C_DOT);
    text(cx - 3, cy - 11, name, tc);

    char buf[5];
    rect(cx - 17, cy + 18, 35, 5, C_PANEL);
    sdec(buf, ax);
    text(cx - 17, cy + 18, buf, tc);
    sdec(buf, ay);
    text(cx + 2, cy + 18, buf, tc);
}

static void draw_panel_dynamic(int n, const PadState* p)
{
    int ox, oy;
    select_pad_fb(n, &ox, &oy);
    u32 b = p->buttons;
    u16 tc = p->connected ? C_TEXT : C_DIM;

    char hex[9];
    hex32(hex, b);
    rect(ox + 90, oy + 4, 33, 5, C_PANEL);
    text(ox + 91, oy + 4, hex, p->connected ? C_DIM : RGB(7, 7, 9));

    if (n == 0)
    {
        // raw KEYINPUT: bits 10-13 carry L2/R2/L3/R3 (active-high)
        char kh[9];
        hex32(kh, REG16(0x04000130));
        rect(ox + 36, oy + 4, 40, 5, C_PANEL);
        text(ox + 36, oy + 4, "KEY", C_DIM);
        text(ox + 50, oy + 4, kh + 4, C_DIM);
    }

    // shoulders: L2 above L, R2 above R
    trigger_bar(ox + 5, oy + 11, 38, 6, p->trig[0], b & K_L2, "L2", tc);
    button_box(ox + 5, oy + 19, 38, 6, b & K_L, "L", tc);
    trigger_bar(ox + 85, oy + 11, 38, 6, p->trig[1], b & K_R2, "R2", tc);
    button_box(ox + 85, oy + 19, 38, 6, b & K_R, "R", tc);

    // d-pad
    int cx = ox + 22, cy = oy + 39;
    rect(cx - 3, cy - 3, 7, 7, C_OFF);
    button_box(cx - 3, cy - 11, 7, 7, b & K_UP, 0, tc);
    button_box(cx - 3, cy + 5, 7, 7, b & K_DOWN, 0, tc);
    button_box(cx - 11, cy - 3, 7, 7, b & K_LEFT, 0, tc);
    button_box(cx + 5, cy - 3, 7, 7, b & K_RIGHT, 0, tc);

    // select / start
    button_box(ox + 47, oy + 36, 16, 7, b & K_SELECT, "SEL", tc);
    button_box(ox + 65, oy + 36, 16, 7, b & K_START, "STA", tc);

    // face buttons
    cx = ox + 105; cy = oy + 39;
    face(cx, cy - 9, b & K_X, "X", tc);
    face(cx - 9, cy, b & K_Y, "Y", tc);
    face(cx + 9, cy, b & K_A, "A", tc);
    face(cx, cy + 9, b & K_B, "B", tc);

    // sticks (L3/R3 = disc highlight)
    stick(ox + 40, oy + 70, p->axes[0], p->axes[1], b & K_L3, b, K_LS_RIGHT, K_LS_LEFT, K_LS_UP, K_LS_DOWN, "L3", tc);
    stick(ox + 88, oy + 70, p->axes[2], p->axes[3], b & K_R3, b, K_RS_RIGHT, K_RS_LEFT, K_RS_UP, K_RS_DOWN, "R3", tc);
}

static void wait_vblank(void)
{
    while (VCOUNT >= 192) {}
    while (VCOUNT < 192) {}
}

int main(void)
{
    POWCNT1 = 0x8203;           // both LCDs + 2D engines, engine A on top

    VRAMCNT_A = 0x80;           // LCDC: engine A framebuffer at 0x06800000
    DISPCNT_A = 0x00020000;     // display mode 2: VRAM A

    VRAMCNT_C = 0x84;           // engine B BG at 0x06200000
    DISPCNT_B = 0x00010000 | 5 | (1 << 11);   // mode 5, BG3 on
    BG3CNT_B = (1 << 14) | (1 << 7) | (1 << 2); // 256x256 direct colour bitmap
    BG3PA_B = 0x100; BG3PB_B = 0; BG3PC_B = 0; BG3PD_B = 0x100;
    BG3X_B = 0; BG3Y_B = 0;

    fb = backbuf[0]; rect(0, 0, 256, 192, C_BG);
    fb = backbuf[1]; rect(0, 0, 256, 192, C_BG);
    copy_screen(VRAM_TOP, backbuf[0]);
    copy_screen(VRAM_BOT, backbuf[1]);

    if (REG32(MPAD_BASE) != MPAD_MAGIC)
    {
        fb = VRAM_TOP;
        text(60, 80, "NO MPAD BLOCK AT 04FFF000", C_BAD);
        text(48, 92, "RUN THIS IN THE MULTI-PAD MELONDS", C_TEXT);
        for (;;) wait_vblank();
    }

    int lastconn[8];
    PadState last[8];
    u16 lastkey = 0xFFFF;
    for (int n = 0; n < 8; n++) lastconn[n] = -1;

    for (;;)
    {
        wait_vblank();
        if (dirty[0]) { copy_screen(VRAM_TOP, backbuf[0]); dirty[0] = 0; }
        if (dirty[1]) { copy_screen(VRAM_BOT, backbuf[1]); dirty[1] = 0; }

        u16 key = REG16(0x04000130);
        for (int n = 0; n < 8; n++)
        {
            PadState p;
            read_pad(n, &p);
            int changed = (lastconn[n] < 0) || memcmp(&p, &last[n], sizeof(p)) || (n == 0 && key != lastkey);
            if (p.connected != lastconn[n])
            {
                draw_panel_static(n, p.connected);
                lastconn[n] = p.connected;
            }
            if (changed)
                draw_panel_dynamic(n, &p);
            last[n] = p;
        }
        lastkey = key;
    }
}
