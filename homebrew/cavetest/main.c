// cavetest: checks the melonDS-mp cheat memory (Action Replay code CF000000 SSSSSSSS).
// Bare metal (no libnds). Load it with cavetest.mch (cheat file) enabled.
//
// The cheat allocates memory at 0x0E000000 and writes a tiny function plus a
// marker into it. This ROM then turns on the MPU with regions that do NOT cover
// 0x0E000000 and checks that it can still read, call, write and rewrite code
// there (the last step also checks JIT invalidation).

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define REG32(a) (*(volatile u32*)(a))
#define REG16(a) (*(volatile u16*)(a))
#define REG8(a)  (*(volatile u8*)(a))

#define CAVE        0x0E000000
#define CAVE32(o)   REG32(CAVE + (o))
#define MPAD_BASE   0x04FFF000

#define RGB(r,g,b) ((u16)((r) | ((g) << 5) | ((b) << 10) | 0x8000))
static const u16 C_BG = RGB(2, 2, 3), C_TEXT = RGB(26, 26, 28), C_DIM = RGB(14, 14, 16);
static const u16 C_GOOD = RGB(6, 26, 8), C_BAD = RGB(28, 6, 6);

void* memset(void* d, int c, unsigned n) { u8* p = d; while (n--) *p++ = (u8)c; return d; }
void* memcpy(void* d, const void* s, unsigned n) { u8* p = d; const u8* q = s; while (n--) *p++ = *q++; return d; }

static u16* const fb = (u16*)0x06800000;

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
    case '=': return "07070";
    case '/': return "11244";
    }
    return "00000";
}

// 2x scaled 3x5 font: 8 px per character, 12 px per line
static void text(int x, int y, const char* s, u16 c)
{
    for (; *s; s++, x += 8)
    {
        const char* g = glyph(*s);
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 3; col++)
                if ((g[row] - '0') & (4 >> col))
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++)
                        {
                            int px = x + col * 2 + dx, py = y + row * 2 + dy;
                            if (px < 256 && py < 192) fb[py * 256 + px] = c;
                        }
    }
}

static char* hex32(char* out, u32 v)
{
    for (int i = 0; i < 8; i++)
    {
        int n = (v >> (28 - 4 * i)) & 0xF;
        out[i] = n < 10 ? '0' + n : 'A' + n - 10;
    }
    out[8] = 0;
    return out;
}

static int line = 0;
static void result(const char* label, u32 value, int ok)
{
    char h[9];
    text(4, 4 + line * 12, label, C_TEXT);
    text(4 + 8 * 9, 4 + line * 12, hex32(h, value), C_DIM);
    text(4 + 8 * 18, 4 + line * 12, ok ? "OK" : "FAIL", ok ? C_GOOD : C_BAD);
    line++;
}

static void wait_vblank(void)
{
    while (REG16(0x04000006) >= 192) {}
    while (REG16(0x04000006) < 192) {}
}

// MPU on with: 0 = I/O+VRAM (0x04000000, 64 MB), 1 = main RAM (0x02000000, 4 MB),
// 2 = BIOS (0xFFFF0000, 32 KB). Nothing covers 0x0E000000 and caches stay off.
static void mpu_enable_without_cave(void)
{
    u32 zero = 0;
    asm volatile("mcr p15, 0, %0, c6, c0, 0" :: "r"(0x04000000 | (25 << 1) | 1));
    asm volatile("mcr p15, 0, %0, c6, c1, 0" :: "r"(0x02000000 | (21 << 1) | 1));
    asm volatile("mcr p15, 0, %0, c6, c2, 0" :: "r"(0xFFFF0000 | (14 << 1) | 1));
    asm volatile("mcr p15, 0, %0, c6, c3, 0" :: "r"(zero));
    asm volatile("mcr p15, 0, %0, c6, c4, 0" :: "r"(zero));
    asm volatile("mcr p15, 0, %0, c6, c5, 0" :: "r"(zero));
    asm volatile("mcr p15, 0, %0, c6, c6, 0" :: "r"(zero));
    asm volatile("mcr p15, 0, %0, c6, c7, 0" :: "r"(zero));
    asm volatile("mcr p15, 0, %0, c5, c0, 2" :: "r"(0x333));   // data: RW for 0-2
    asm volatile("mcr p15, 0, %0, c5, c0, 3" :: "r"(0x333));   // code: RW for 0-2
    asm volatile("mcr p15, 0, %0, c2, c0, 0" :: "r"(zero));    // no data cache
    asm volatile("mcr p15, 0, %0, c2, c0, 1" :: "r"(zero));    // no code cache
    asm volatile("mcr p15, 0, %0, c3, c0, 0" :: "r"(zero));    // no write buffer
    u32 ctrl;
    asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(ctrl));
    ctrl &= ~((1u << 2) | (1u << 12));
    ctrl |= 1;
    asm volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(ctrl));
}

typedef u32 (*cave_fn)(void);

int main(void)
{
    REG16(0x04000304) = 0x8203;     // LCDs + 2D engines on, engine A on top
    REG8(0x04000240) = 0x80;        // VRAM A as LCDC framebuffer
    REG32(0x04000000) = 0x00020000; // display mode 2: VRAM A
    for (int i = 0; i < 256 * 192; i++) fb[i] = C_BG;

    text(4, 168, "CHEAT MEMORY TEST  0E000000", C_DIM);
    text(4, 180, "CHEAT: CF000000 + E0000000", C_DIM);

    // the cheat runs on the ARM7 VBlank IRQ; give it a few frames
    int frames = 0;
    while (CAVE32(8) != 0x12345678 && frames < 300) { wait_vblank(); frames++; }
    result("MARKER", CAVE32(8), CAVE32(8) == 0x12345678);
    if (CAVE32(8) != 0x12345678)
    {
        text(4, 4 + line * 12, "ENABLE CAVETEST.MCH CHEAT", C_BAD);
        for (;;) wait_vblank();
    }

    mpu_enable_without_cave();
    result("MPU ON", 0x0E000000, 1);

    // data read through the MPU (data abort without the override)
    u32 v = CAVE32(12);
    result("READ", v, v == 0xCAFEBABE);

    // call the cheat's function (prefetch abort without the override)
    u32 r = ((cave_fn)CAVE)();
    result("CALL", r, r == 0x42);

    // write + read back
    CAVE32(0x100) = 0xA5A55A5A;
    v = CAVE32(0x100);
    result("WRITE", v, v == 0xA5A55A5A);

    // rewrite the function (mov r0,#0x99 ; bx lr) and call it again:
    // with the JIT on, the old compiled block must be thrown away
    CAVE32(0) = 0xE3A00099;
    r = ((cave_fn)CAVE)();
    result("REWRITE", r, r == 0x99);

    // allocated size as reported in the multi-pad block (+0x0C)
    u32 size = REG32(MPAD_BASE + 0x0C);
    result("SIZE", size, size >= 0x10000);

    // the cheat rewrites the original code every frame: the next call sees 0x42 again
    wait_vblank(); wait_vblank();
    r = ((cave_fn)CAVE)();
    result("CHEAT RUN", r, r == 0x42);

    for (;;) wait_vblank();
}
