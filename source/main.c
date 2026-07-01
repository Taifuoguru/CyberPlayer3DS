/*
 * =====================================================
 * 3DS CYBER PLAYER MEGA v8.0 - PERFECT VFD PLAYER
 * - Baslangicta dosya yoneticisi
 * - Kenwood/DPX tarz VFD grafik motoru
 * - Ust ekrandan alt ekrana CD takma animasyonu
 * - Ayarlar menusu + cok bantli EQ + hiz/volume/CRT/kapak modu
 * - Gelismis alt UI, gear tusu, dosya listesi
 * - Dolphin, tunnel, radar, starfield, cassette, oscilloscope animasyonlari
 * - Sarki kapagi modu: MP3 icinden resim parse etmez, sarki adindan VFD cover uretir
 * - Kamera ve rotate duzeltmeleri
 * =====================================================
 */

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <math.h>
#include <mad.h>
#include <time.h>

#define SES_KANALI              0
#define BUFFER_BOYUTU           (4096 * 4)
#define HAM_SES_BOYUTU          (1152 * 2 * 2 * 3)
#define MAKS_SARKI              500
#define CAM_W                   400
#define CAM_H                   240
#define CAM_BUF_SIZE            (CAM_W * CAM_H * 2)
#define TRIG_TABLE_SIZE         8192

#define VIZ_TIP_SAYISI          15
#define VIZ_QUAD_MIX_TIPI       14
#define QUAD_VIZ_SAYISI         4

#define COL_CYAN_R              10
#define COL_CYAN_G              240
#define COL_CYAN_B              255

#define COL_AMBER_R             255
#define COL_AMBER_G             180
#define COL_AMBER_B             20

#define COL_MAG_R               255
#define COL_MAG_G               30
#define COL_MAG_B               255

#define COL_GRN_R               20
#define COL_GRN_G               255
#define COL_GRN_B               120

#define COL_RED_R               255
#define COL_RED_G               70
#define COL_RED_B               35

typedef enum {
    UI_FILE_BROWSER = 0,
    UI_PLAYER = 1,
    UI_SETTINGS = 2
} UiMode;

typedef struct {
    bool play;
    bool prev;
    bool next;
    bool rew;
    bool ff;
} ButtonState;

typedef struct {
    char name[256];
    char path[512];
    bool isDirectory;
} BrowserEntry;

typedef struct {
    char path[512];
} PlaylistEntry;

BrowserEntry browserEntries[MAKS_SARKI];
int browserEntryCount = 0;

PlaylistEntry playingPlaylist[MAKS_SARKI];
volatile int playingPlaylistCount = 0;

volatile int aktifSarkiIdx = 0;
int fileCursor = 0;
int fileScroll = 0;
char currentDir[512] = "sdmc:/";

float peakHold[44] = {0};
int peakHoldDecay[44] = {0};

FILE* mp3Dosyasi = NULL;
unsigned char* mp3Buffer = NULL;
s16* pcmBufferA = NULL;
s16* pcmBufferB = NULL;
ndspWaveBuf dalgaBlokuA;
ndspWaveBuf dalgaBlokuB;

volatile bool hangiBufferA = true;
volatile bool caliyor = false;

struct mad_stream Stream;
struct mad_frame Frame;
struct mad_synth Synth;

unsigned long tahminiToplamBoyut = 1;
int anlikSaniye = 0;
float sesMiksaji[12];

int viz_anlik_genlik = 0;
float animasyonFaz = 0.0f;
float globalPulse = 0.0f;

int eqBand[7] = {100, 100, 100, 100, 100, 100, 100};
int eqPresetIdx = 0;
int masterVolume = 100;
int playbackRatePct = 100;
int crtFosforGucu = 150;

bool shuffleMode = false;
int repeatMode = 0;
bool otomatikDegistir = true;
bool coverMode = false;

const char* eqPresetNames[] = {"FLAT", "ROCK", "POP", "JAZZ", "BASS", "VOCAL"};

Thread audioThread = NULL;
volatile bool audioThreadRunning = false;
volatile bool audioThreadPlaying = false;

bool kameraAcik = false;
bool dikModAktif = false;
bool rotateModFull = false;
u16* camBuffer = NULL;
u16* camBufferPrev = NULL;
Handle camEvent = 0;

ButtonState buttons;
UiMode uiMode = UI_FILE_BROWSER;

static float sinTable[TRIG_TABLE_SIZE];
static float cosTable[TRIG_TABLE_SIZE];

int aktifVizTipi = 0;
int aktifVizStili = 1;
int aktifVizMod = 0;
int aktifRenkModu = 0;
unsigned long frameSayaci = 0;

int quadVizTipi[QUAD_VIZ_SAYISI] = {0, 1, 2, 3};
int quadVizStili[QUAD_VIZ_SAYISI] = {0, 1, 2, 3};
int quadVizModu[QUAD_VIZ_SAYISI] = {0, 1, 2, 0};
int quadRenkModu[QUAD_VIZ_SAYISI] = {0, 1, 2, 3};

int cdAnimFrame = 0;
bool cdAnimAktif = false;

int settingsCursor = 0;
int coverSeed = 1;

const char* vfdIsimleri1[] = {"VFD", "CYBER", "NEON", "RETRO", "SONY", "KNWD", "PIONR", "ALPIN"};
const char* vfdIsimleri2[] = {"SCOPE", "EQ", "PULSE", "WAVE", "MTRIX", "RADAR", "GRID", "HELIX", "CORE", "BEAM", "DOLPH", "STAR", "TAPE", "ORBIT"};
char anlikVizIsmi[64] = "KENWOOD VFD v8.0";

static inline float sin_fast(float ang);
static inline float cos_fast(float ang);

const char* getBasename(const char* path);
bool isPathADirectory(const char* path);
void loadDirectory(const char* path);

void pixelDraw(u8* fb, int x, int y, u8 r, u8 g, u8 b, bool top);
void gfxRect(u8* fb, int x, int y, int w, int h, u8 r, u8 g, u8 b, bool top);
void rectFill(u8* fb, int x, int y, int w, int h, u8 r, u8 g, u8 b, bool top);
void gfxLine(u8* fb, int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b, bool top);
void gfxCircle(u8* fb, int cx, int cy, int radius, u8 r, u8 g, u8 b, bool top);
void gfxDisc(u8* fb, int cx, int cy, int radius, u8 r, u8 g, u8 b, bool top);
void gfxGlowPixel(u8* fb, int x, int y, u8 r, u8 g, u8 b, bool top);
void textDraw(u8* fb, const char* str, int x, int y, u8 r, u8 g, u8 b, bool top);
void textDrawCentered(u8* fb, const char* str, int y, u8 r, u8 g, u8 b, bool top);
void renderFrame(void);

void trigInit(void) {
    for (int i = 0; i < TRIG_TABLE_SIZE; i++) {
        float a = (2.0f * M_PI * i) / (float)TRIG_TABLE_SIZE;
        sinTable[i] = sinf(a);
        cosTable[i] = cosf(a);
    }
}

static inline float sin_fast(float ang) {
    float a = fmodf(ang, 2.0f * M_PI);
    if (a < 0) a += 2.0f * M_PI;
    return sinTable[(int)(a / (2.0f * M_PI) * TRIG_TABLE_SIZE) & (TRIG_TABLE_SIZE - 1)];
}

static inline float cos_fast(float ang) {
    float a = fmodf(ang, 2.0f * M_PI);
    if (a < 0) a += 2.0f * M_PI;
    return cosTable[(int)(a / (2.0f * M_PI) * TRIG_TABLE_SIZE) & (TRIG_TABLE_SIZE - 1)];
}

signed short madScale(mad_fixed_t sample) {
    if (sample >= MAD_F_ONE) return 32767;
    if (sample <= -MAD_F_ONE) return -32768;
    return (signed short)(sample >> (MAD_F_FRACBITS - 15));
}

char normalizeChar(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if ((unsigned char)c >= 128) return ' ';
    return c;
}

void cleanTrackName(const char* src, char* dst, int maxLen) {
    int di = 0;

    for (int i = 0; src[i] && di < maxLen - 1; i++) {
        char c = src[i];

        if (c == '.') {
            if (strstr(src + i, ".mp3") || strstr(src + i, ".MP3")) break;
        }

        if (c == '_' || c == '-') c = ' ';
        c = normalizeChar(c);

        dst[di++] = c;
    }

    dst[di] = 0;
}

int hashString(const char* s) {
    int h = 5381;

    for (int i = 0; s[i]; i++) {
        h = ((h << 5) + h) + s[i];
    }

    if (h < 0) h = -h;
    return h;
}

void applyEqPreset(int preset) {
    switch (preset) {
        case 0:
            for (int i = 0; i < 7; i++) eqBand[i] = 100;
            break;

        case 1:
            eqBand[0] = 130; eqBand[1] = 120; eqBand[2] = 100; eqBand[3] = 95; eqBand[4] = 105; eqBand[5] = 120; eqBand[6] = 130;
            break;

        case 2:
            eqBand[0] = 110; eqBand[1] = 115; eqBand[2] = 105; eqBand[3] = 100; eqBand[4] = 115; eqBand[5] = 120; eqBand[6] = 110;
            break;

        case 3:
            eqBand[0] = 105; eqBand[1] = 115; eqBand[2] = 125; eqBand[3] = 115; eqBand[4] = 105; eqBand[5] = 95; eqBand[6] = 90;
            break;

        case 4:
            eqBand[0] = 150; eqBand[1] = 140; eqBand[2] = 125; eqBand[3] = 100; eqBand[4] = 90; eqBand[5] = 85; eqBand[6] = 80;
            break;

        default:
            eqBand[0] = 90; eqBand[1] = 95; eqBand[2] = 110; eqBand[3] = 135; eqBand[4] = 125; eqBand[5] = 105; eqBand[6] = 95;
            break;
    }
}

s16 audioProcessSample(s16 input, int channel) {
    static float lpL = 0.0f;
    static float hpL = 0.0f;
    static float lpR = 0.0f;
    static float hpR = 0.0f;
    static float prevL = 0.0f;
    static float prevR = 0.0f;

    float x = (float)input;

    float* lp = channel == 0 ? &lpL : &lpR;
    float* hp = channel == 0 ? &hpL : &hpR;
    float* prev = channel == 0 ? &prevL : &prevR;

    *lp = (*lp * 0.94f) + (x * 0.06f);
    *hp = 0.92f * (*hp + x - *prev);
    *prev = x;

    float bass = *lp;
    float treble = *hp;
    float mid = x - bass - treble;

    float bassGain = (eqBand[0] + eqBand[1]) * 0.005f;
    float midGain = (eqBand[2] + eqBand[3] + eqBand[4]) * 0.003333f;
    float trebleGain = (eqBand[5] + eqBand[6]) * 0.005f;
    float volGain = masterVolume / 100.0f;

    float y = ((bass * bassGain) + (mid * midGain) + (treble * trebleGain)) * volGain;

    if (y > 32767.0f) y = 32767.0f;
    if (y < -32768.0f) y = -32768.0f;

    return (s16)y;
}

void sesKanaliniYenidenBaslat(void) {
    ndspChnReset(SES_KANALI);

    if (pcmBufferA) memset(pcmBufferA, 0, HAM_SES_BOYUTU);
    if (pcmBufferB) memset(pcmBufferB, 0, HAM_SES_BOYUTU);

    dalgaBlokuA.status = NDSP_WBUF_FREE;
    dalgaBlokuB.status = NDSP_WBUF_FREE;
    hangiBufferA = true;

    ndspChnSetFormat(SES_KANALI, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetInterp(SES_KANALI, NDSP_INTERP_LINEAR);
    ndspChnSetRate(SES_KANALI, 44100.0f * ((float)playbackRatePct / 100.0f));

    memset(sesMiksaji, 0, sizeof(sesMiksaji));
    sesMiksaji[0] = 1.0f;
    sesMiksaji[1] = 1.0f;
    ndspChnSetMix(SES_KANALI, sesMiksaji);
}

const char* getBasename(const char* path) {
    const char* lastSlash = strrchr(path, '/');
    if (!lastSlash) lastSlash = strrchr(path, '\\');
    return lastSlash ? (lastSlash + 1) : path;
}

bool isPathADirectory(const char* path) {
    DIR* testDir = opendir(path);
    if (testDir) {
        closedir(testDir);
        return true;
    }
    return false;
}

void loadDirectory(const char* path) {
    DIR* dir = opendir(path);
    if (!dir) {
        if (strcmp(path, "sdmc:/") == 0) {
            strncpy(currentDir, "romfs:/", sizeof(currentDir));
            dir = opendir(currentDir);
        }
    }
    
    if (!dir) {
        browserEntryCount = 0;
        return;
    }
    
    browserEntryCount = 0;
    strncpy(currentDir, path, sizeof(currentDir) - 1);
    currentDir[sizeof(currentDir) - 1] = '\0';
    
    int len = strlen(currentDir);
    if (len > 0 && currentDir[len - 1] != '/' && strcmp(currentDir, "sdmc:") != 0 && strcmp(currentDir, "romfs:") != 0) {
        if (len < sizeof(currentDir) - 2) {
            currentDir[len] = '/';
            currentDir[len + 1] = '\0';
        }
    }
    
    bool isRoot = (strcmp(currentDir, "sdmc:/") == 0 || strcmp(currentDir, "romfs:/") == 0 ||
                   strcmp(currentDir, "sdmc:") == 0 || strcmp(currentDir, "romfs:") == 0);
    
    if (!isRoot) {
        browserEntries[browserEntryCount].isDirectory = true;
        strncpy(browserEntries[browserEntryCount].name, "..", sizeof(browserEntries[browserEntryCount].name));
        
        char parent[512];
        strncpy(parent, currentDir, sizeof(parent));
        int plen = strlen(parent);
        if (plen > 0 && parent[plen - 1] == '/') {
            parent[plen - 1] = '\0';
        }
        
        char* lastSlash = strrchr(parent, '/');
        if (lastSlash) {
            if (lastSlash == parent + 5 && (strncmp(parent, "sdmc:", 5) == 0 || strncmp(parent, "romfs:", 6) == 0)) {
                *(lastSlash + 1) = '\0';
            } else {
                *lastSlash = '\0';
            }
        } else {
            strncpy(parent, "sdmc:/", sizeof(parent));
        }
        
        strncpy(browserEntries[browserEntryCount].path, parent, sizeof(browserEntries[browserEntryCount].path));
        browserEntryCount++;
    }
    
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL && browserEntryCount < MAKS_SARKI) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        
        char fullPath[512];
        snprintf(fullPath, sizeof(fullPath), "%s%s", currentDir, ent->d_name);
        
        bool isDir = false;
        #ifdef DT_DIR
        if (ent->d_type == DT_DIR) {
            isDir = true;
        } else if (ent->d_type == DT_UNKNOWN || ent->d_type == 0) {
            isDir = isPathADirectory(fullPath);
        }
        #else
        isDir = isPathADirectory(fullPath);
        #endif
        
        if (isDir) {
            browserEntries[browserEntryCount].isDirectory = true;
            strncpy(browserEntries[browserEntryCount].name, ent->d_name, sizeof(browserEntries[browserEntryCount].name));
            strncpy(browserEntries[browserEntryCount].path, fullPath, sizeof(browserEntries[browserEntryCount].path));
            browserEntryCount++;
        }
    }
    
    rewinddir(dir);
    
    while ((ent = readdir(dir)) != NULL && browserEntryCount < MAKS_SARKI) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        
        char fullPath[512];
        snprintf(fullPath, sizeof(fullPath), "%s%s", currentDir, ent->d_name);
        
        bool isDir = false;
        #ifdef DT_DIR
        if (ent->d_type == DT_DIR) {
            isDir = true;
        } else if (ent->d_type == DT_UNKNOWN || ent->d_type == 0) {
            isDir = isPathADirectory(fullPath);
        }
        #else
        isDir = isPathADirectory(fullPath);
        #endif
        
        if (!isDir && (strstr(ent->d_name, ".mp3") || strstr(ent->d_name, ".MP3"))) {
            browserEntries[browserEntryCount].isDirectory = false;
            strncpy(browserEntries[browserEntryCount].name, ent->d_name, sizeof(browserEntries[browserEntryCount].name));
            strncpy(browserEntries[browserEntryCount].path, fullPath, sizeof(browserEntries[browserEntryCount].path));
            browserEntryCount++;
        }
    }
    
    closedir(dir);
}

bool sarkiYukle(const char* dosyaAdi) {
    if (!dosyaAdi) return false;

    if (mp3Dosyasi) {
        fclose(mp3Dosyasi);
        mad_synth_finish(&Synth);
        mad_frame_finish(&Frame);
        mad_stream_finish(&Stream);
    }

    mp3Dosyasi = fopen(dosyaAdi, "rb");
    if (!mp3Dosyasi) return false;

    fseek(mp3Dosyasi, 0, SEEK_END);
    tahminiToplamBoyut = ftell(mp3Dosyasi);
    fseek(mp3Dosyasi, 0, SEEK_SET);

    mad_stream_init(&Stream);
    mad_frame_init(&Frame);
    mad_synth_init(&Synth);

    sesKanaliniYenidenBaslat();
    viz_anlik_genlik = 0;

    coverSeed = hashString(dosyaAdi);
    cdAnimAktif = true;
    cdAnimFrame = 0;

    return true;
}

void sarkiZamaniniDegistir(int saniyeFarki) {
    if (!mp3Dosyasi) return;

    long yeniPos = ftell(mp3Dosyasi) + (long)(saniyeFarki * 16000);

    if (yeniPos < 0) yeniPos = 0;
    if (yeniPos > (long)tahminiToplamBoyut - 100) yeniPos = (long)tahminiToplamBoyut - 100;

    fseek(mp3Dosyasi, yeniPos, SEEK_SET);
    mad_stream_finish(&Stream);
    mad_stream_init(&Stream);
}

void audioProducerThread(void* arg) {
    audioThreadRunning = true;

    while (audioThreadRunning) {
        if (!audioThreadPlaying || !mp3Dosyasi) {
            svcSleepThread(1000000ULL);
            continue;
        }

        ndspWaveBuf* ab = hangiBufferA ? &dalgaBlokuA : &dalgaBlokuB;
        s16* ap = hangiBufferA ? pcmBufferA : pcmBufferB;

        if (ab->status == NDSP_WBUF_DONE || ab->status == NDSP_WBUF_FREE) {
            long toplamOrnek = 0;
            int kareSayisi = 0;

            while (kareSayisi < 3) {
                if (Stream.buffer == NULL || Stream.error == MAD_ERROR_BUFLEN) {
                    size_t kalanVeri = (Stream.next_frame != NULL) ? (size_t)(Stream.bufend - Stream.next_frame) : 0;

                    if (kalanVeri > 0) {
                        memmove(mp3Buffer, Stream.next_frame, kalanVeri);
                    }

                    size_t okunan = fread(mp3Buffer + kalanVeri, 1, BUFFER_BOYUTU - kalanVeri, mp3Dosyasi);

                    if (okunan == 0) {
                        if (shuffleMode && playingPlaylistCount > 1) {
                            aktifSarkiIdx = rand() % playingPlaylistCount;
                            sarkiYukle(playingPlaylist[aktifSarkiIdx].path);
                        } else if (repeatMode == 1) {
                            fseek(mp3Dosyasi, 0, SEEK_SET);
                            mad_stream_finish(&Stream);
                            mad_stream_init(&Stream);
                        } else if (playingPlaylistCount > 0) {
                            aktifSarkiIdx = (aktifSarkiIdx + 1) % playingPlaylistCount;
                            sarkiYukle(playingPlaylist[aktifSarkiIdx].path);
                        }
                        break;
                    }

                    mad_stream_buffer(&Stream, mp3Buffer, okunan + kalanVeri);
                }

                if (mad_frame_decode(&Frame, &Stream) == 0) {
                    mad_synth_frame(&Synth, &Frame);

                    for (int i = 0; i < Synth.pcm.length; i++) {
                        s16 sL = madScale(Synth.pcm.samples[0][i]);
                        s16 sR = (Synth.pcm.channels == 2) ? madScale(Synth.pcm.samples[1][i]) : sL;

                        sL = audioProcessSample(sL, 0);
                        sR = audioProcessSample(sR, 1);

                        if (toplamOrnek + 1 < HAM_SES_BOYUTU / 2) {
                            ap[toplamOrnek++] = sL;
                            ap[toplamOrnek++] = sR;
                        }

                        if ((i & 1023) == 0) {
                            viz_anlik_genlik = (abs(sL) + abs(sR)) / 1800;
                            if (viz_anlik_genlik > 120) viz_anlik_genlik = 120;
                        }
                    }

                    kareSayisi++;
                } else if (!MAD_RECOVERABLE(Stream.error)) {
                    break;
                }
            }

            if (toplamOrnek > 0) {
                DSP_FlushDataCache(ap, toplamOrnek * sizeof(s16));
                ab->nsamples = toplamOrnek / 2;
                ab->status = NDSP_WBUF_FREE;
                ndspChnWaveBufAdd(SES_KANALI, ab);
                hangiBufferA = !hangiBufferA;
            }
        } else {
            svcSleepThread(2000000ULL);
        }

        svcSleepThread(1000000ULL);
    }

    threadExit(0);
}

void startAudioThread(void) {
    if (audioThread) return;

    audioThreadRunning = true;
    audioThreadPlaying = caliyor;
    audioThread = threadCreate(audioProducerThread, NULL, 0x4000, 0x18, -2, false);
}

void stopAudioThread(void) {
    if (!audioThread) return;

    audioThreadRunning = false;
    threadJoin(audioThread, U64_MAX);
    threadFree(audioThread);
    audioThread = NULL;
}

void pixelDraw(u8* fb, int x, int y, u8 r, u8 g, u8 b, bool top) {
    int W = top ? 400 : 320;
    int H = 240;

    if (rotateModFull) {
        x = W - 1 - x;
        y = H - 1 - y;
    }

    if (x < 0 || x >= W || y < 0 || y >= H) return;

    int idx = ((H - 1 - y) + x * H) * 3;
    fb[idx] = b;
    fb[idx + 1] = g;
    fb[idx + 2] = r;
}

void gfxRect(u8* fb, int x, int y, int w, int h, u8 r, u8 g, u8 b, bool top) {
    for (int i = x; i < x + w; i++) {
        for (int j = y; j < y + h; j++) {
            pixelDraw(fb, i, j, r, g, b, top);
        }
    }
}

void rectFill(u8* fb, int x, int y, int w, int h, u8 r, u8 g, u8 b, bool top) {
    gfxRect(fb, x, y, w, h, r, g, b, top);
}

void gfxLine(u8* fb, int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b, bool top) {
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        pixelDraw(fb, x0, y0, r, g, b, top);

        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;

        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }

        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfxCircle(u8* fb, int cx, int cy, int radius, u8 r, u8 g, u8 b, bool top) {
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int d = x * x + y * y;

            if (d <= radius * radius && d >= (radius - 2) * (radius - 2)) {
                pixelDraw(fb, cx + x, cy + y, r, g, b, top);
            }
        }
    }
}

void gfxDisc(u8* fb, int cx, int cy, int radius, u8 r, u8 g, u8 b, bool top) {
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius * radius) {
                pixelDraw(fb, cx + x, cy + y, r, g, b, top);
            }
        }
    }
}

void gfxGlowPixel(u8* fb, int x, int y, u8 r, u8 g, u8 b, bool top) {
    pixelDraw(fb, x, y, r, g, b, top);
    pixelDraw(fb, x + 1, y, r / 2, g / 2, b / 2, top);
    pixelDraw(fb, x - 1, y, r / 2, g / 2, b / 2, top);
    pixelDraw(fb, x, y + 1, r / 2, g / 2, b / 2, top);
    pixelDraw(fb, x, y - 1, r / 2, g / 2, b / 2, top);
}

void drawGear(u8* fb, int cx, int cy, u8 r, u8 g, u8 b, bool top) {
    gfxCircle(fb, cx, cy, 10, r, g, b, top);
    gfxCircle(fb, cx, cy, 4, r, g, b, top);

    for (int i = 0; i < 8; i++) {
        float a = i * 0.785f + animasyonFaz * 0.03f;
        int x1 = cx + (int)(cos_fast(a) * 11.0f);
        int y1 = cy + (int)(sin_fast(a) * 11.0f);
        int x2 = cx + (int)(cos_fast(a) * 15.0f);
        int y2 = cy + (int)(sin_fast(a) * 15.0f);
        gfxLine(fb, x1, y1, x2, y2, r, g, b, top);
    }
}

void charDraw(u8* fb, char ch, int x, int y, u8 cr, u8 cg, u8 cb, bool top) {
    int f[7] = {0};
    ch = normalizeChar(ch);

    switch (ch) {
        case 'A': f[0]=14;f[1]=17;f[2]=17;f[3]=31;f[4]=17;f[5]=17;f[6]=17; break;
        case 'B': f[0]=30;f[1]=17;f[2]=17;f[3]=30;f[4]=17;f[5]=17;f[6]=30; break;
        case 'C': f[0]=14;f[1]=17;f[2]=16;f[3]=16;f[4]=16;f[5]=17;f[6]=14; break;
        case 'D': f[0]=30;f[1]=17;f[2]=17;f[3]=17;f[4]=17;f[5]=17;f[6]=30; break;
        case 'E': f[0]=31;f[1]=16;f[2]=16;f[3]=30;f[4]=16;f[5]=16;f[6]=31; break;
        case 'F': f[0]=31;f[1]=16;f[2]=16;f[3]=30;f[4]=16;f[5]=16;f[6]=16; break;
        case 'G': f[0]=14;f[1]=17;f[2]=16;f[3]=23;f[4]=17;f[5]=17;f[6]=14; break;
        case 'H': f[0]=17;f[1]=17;f[2]=17;f[3]=31;f[4]=17;f[5]=17;f[6]=17; break;
        case 'I': f[0]=14;f[1]=4;f[2]=4;f[3]=4;f[4]=4;f[5]=4;f[6]=14; break;
        case 'J': f[0]=7;f[1]=2;f[2]=2;f[3]=2;f[4]=18;f[5]=18;f[6]=12; break;
        case 'K': f[0]=17;f[1]=18;f[2]=20;f[3]=24;f[4]=20;f[5]=18;f[6]=17; break;
        case 'L': f[0]=16;f[1]=16;f[2]=16;f[3]=16;f[4]=16;f[5]=16;f[6]=31; break;
        case 'M': f[0]=17;f[1]=27;f[2]=21;f[3]=21;f[4]=17;f[5]=17;f[6]=17; break;
        case 'N': f[0]=17;f[1]=25;f[2]=21;f[3]=19;f[4]=17;f[5]=17;f[6]=17; break;
        case 'O': f[0]=14;f[1]=17;f[2]=17;f[3]=17;f[4]=17;f[5]=17;f[6]=14; break;
        case 'P': f[0]=30;f[1]=17;f[2]=17;f[3]=30;f[4]=16;f[5]=16;f[6]=16; break;
        case 'Q': f[0]=14;f[1]=17;f[2]=17;f[3]=17;f[4]=21;f[5]=18;f[6]=13; break;
        case 'R': f[0]=30;f[1]=17;f[2]=17;f[3]=30;f[4]=20;f[5]=18;f[6]=17; break;
        case 'S': f[0]=15;f[1]=16;f[2]=16;f[3]=14;f[4]=1;f[5]=1;f[6]=30; break;
        case 'T': f[0]=31;f[1]=4;f[2]=4;f[3]=4;f[4]=4;f[5]=4;f[6]=4; break;
        case 'U': f[0]=17;f[1]=17;f[2]=17;f[3]=17;f[4]=17;f[5]=17;f[6]=14; break;
        case 'V': f[0]=17;f[1]=17;f[2]=17;f[3]=17;f[4]=17;f[5]=10;f[6]=4; break;
        case 'W': f[0]=17;f[1]=17;f[2]=17;f[3]=21;f[4]=21;f[5]=21;f[6]=10; break;
        case 'X': f[0]=17;f[1]=17;f[2]=10;f[3]=4;f[4]=10;f[5]=17;f[6]=17; break;
        case 'Y': f[0]=17;f[1]=17;f[2]=10;f[3]=4;f[4]=4;f[5]=4;f[6]=4; break;
        case 'Z': f[0]=31;f[1]=1;f[2]=2;f[3]=4;f[4]=8;f[5]=16;f[6]=31; break;

        case '0': f[0]=14;f[1]=17;f[2]=19;f[3]=21;f[4]=25;f[5]=17;f[6]=14; break;
        case '1': f[0]=4;f[1]=12;f[2]=4;f[3]=4;f[4]=4;f[5]=4;f[6]=14; break;
        case '2': f[0]=14;f[1]=17;f[2]=1;f[3]=2;f[4]=4;f[5]=8;f[6]=31; break;
        case '3': f[0]=30;f[1]=1;f[2]=1;f[3]=14;f[4]=1;f[5]=1;f[6]=30; break;
        case '4': f[0]=2;f[1]=6;f[2]=10;f[3]=18;f[4]=31;f[5]=2;f[6]=2; break;
        case '5': f[0]=31;f[1]=16;f[2]=16;f[3]=30;f[4]=1;f[5]=1;f[6]=30; break;
        case '6': f[0]=14;f[1]=16;f[2]=16;f[3]=30;f[4]=17;f[5]=17;f[6]=14; break;
        case '7': f[0]=31;f[1]=1;f[2]=2;f[3]=4;f[4]=8;f[5]=8;f[6]=8; break;
        case '8': f[0]=14;f[1]=17;f[2]=17;f[3]=14;f[4]=17;f[5]=17;f[6]=14; break;
        case '9': f[0]=14;f[1]=17;f[2]=17;f[3]=15;f[4]=1;f[5]=1;f[6]=14; break;

        case ':': f[0]=0;f[1]=4;f[2]=4;f[3]=0;f[4]=4;f[5]=4;f[6]=0; break;
        case '-': f[0]=0;f[1]=0;f[2]=0;f[3]=31;f[4]=0;f[5]=0;f[6]=0; break;
        case '.': f[0]=0;f[1]=0;f[2]=0;f[3]=0;f[4]=0;f[5]=12;f[6]=12; break;
        case '_': f[0]=0;f[1]=0;f[2]=0;f[3]=0;f[4]=0;f[5]=0;f[6]=31; break;
        case '<': f[0]=1;f[1]=2;f[2]=4;f[3]=8;f[4]=4;f[5]=2;f[6]=1; break;
        case '>': f[0]=16;f[1]=8;f[2]=4;f[3]=2;f[4]=4;f[5]=8;f[6]=16; break;
        case '|': f[0]=4;f[1]=4;f[2]=4;f[3]=4;f[4]=4;f[5]=4;f[6]=4; break;
        case '/': f[0]=1;f[1]=1;f[2]=2;f[3]=4;f[4]=8;f[5]=16;f[6]=16; break;
        case '+': f[0]=0;f[1]=4;f[2]=4;f[3]=31;f[4]=4;f[5]=4;f[6]=0; break;
        case '%': f[0]=24;f[1]=25;f[2]=2;f[3]=4;f[4]=8;f[5]=19;f[6]=3; break;
        case ' ': break;

        default:
            f[0]=31;f[1]=17;f[2]=1;f[3]=6;f[4]=4;f[5]=0;f[6]=4;
            break;
    }

    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if ((f[row] >> (4 - col)) & 1) {
                gfxGlowPixel(fb, x + col, y + row, cr, cg, cb, top);
            }
        }
    }
}

void textDraw(u8* fb, const char* str, int x, int y, u8 r, u8 g, u8 b, bool top) {
    int maxChars = top ? 48 : 38;

    for (int i = 0; str[i] && i < maxChars; i++) {
        charDraw(fb, str[i], x + i * 7, y, r, g, b, top);
    }
}

void textDrawCentered(u8* fb, const char* str, int y, u8 r, u8 g, u8 b, bool top) {
    int W = top ? 400 : 320;
    int len = strlen(str);

    if (top && len > 48) len = 48;
    if (!top && len > 38) len = 38;

    int x = (W - len * 7) / 2;
    if (x < 0) x = 0;

    textDraw(fb, str, x, y, r, g, b, top);
}

void getColorByMode(int renkModu, u8* r, u8* g, u8* b) {
    switch (renkModu) {
        case 0: *r = COL_CYAN_R; *g = COL_CYAN_G; *b = COL_CYAN_B; break;
        case 1: *r = COL_AMBER_R; *g = COL_AMBER_G; *b = COL_AMBER_B; break;
        case 2: *r = COL_MAG_R; *g = COL_MAG_G; *b = COL_MAG_B; break;
        case 3: *r = COL_GRN_R; *g = COL_GRN_G; *b = COL_GRN_B; break;
        default: *r = COL_RED_R; *g = COL_RED_G; *b = COL_RED_B; break;
    }
}

void getMColor(u8* r, u8* g, u8* b) {
    getColorByMode(aktifRenkModu, r, g, b);
}

void quadMixYenile(void) {
    for (int i = 0; i < QUAD_VIZ_SAYISI; i++) {
        quadVizTipi[i] = rand() % (VIZ_TIP_SAYISI - 1);
        quadVizStili[i] = rand() % 5;
        quadVizModu[i] = rand() % 3;
        quadRenkModu[i] = rand() % 5;
    }
}

void rastgeleAnimasyonSec(void) {
    aktifVizTipi = rand() % VIZ_TIP_SAYISI;
    aktifVizStili = rand() % 5;
    aktifVizMod = rand() % 3;
    aktifRenkModu = rand() % 5;

    if (aktifVizTipi == VIZ_QUAD_MIX_TIPI) {
        quadMixYenile();
        snprintf(anlikVizIsmi, sizeof(anlikVizIsmi), "KENWOOD QUAD-MIX");
        return;
    }

    snprintf(
        anlikVizIsmi,
        sizeof(anlikVizIsmi),
        "%s-%s v%d.%d",
        vfdIsimleri1[rand() % 8],
        vfdIsimleri2[rand() % 14],
        aktifVizTipi,
        aktifVizStili
    );
}

void crtEkranTemizle(u8* fb, bool top, bool kameraKullanimda) {
    int toplam = (top ? 400 : 320) * 240 * 3;

    if (kameraKullanimda || crtFosforGucu == 0) {
        memset(fb, 0, toplam);
        return;
    }

    for (int i = 0; i < toplam; i++) {
        fb[i] = (fb[i] * crtFosforGucu) >> 8;
    }
}

void drawScanlines(u8* fb, bool top) {
    int W = top ? 400 : 320;

    for (int x = 0; x < W; x++) {
        for (int y = 0; y < 240; y += 2) {
            int idx = ((239 - y) + x * 240) * 3;

            if (idx >= 0 && idx + 2 < W * 240 * 3) {
                fb[idx] = (fb[idx] * 68) / 100;
                fb[idx + 1] = (fb[idx + 1] * 68) / 100;
                fb[idx + 2] = (fb[idx + 2] * 68) / 100;
            }
        }
    }
}

void kameraBaslat(void) {
    if (kameraAcik) return;

    camInit();

    camBuffer = (u16*)linearAlloc(CAM_BUF_SIZE);
    camBufferPrev = (u16*)linearAlloc(CAM_BUF_SIZE);

    if (!camBuffer || !camBufferPrev) {
        if (camBuffer) linearFree(camBuffer);
        if (camBufferPrev) linearFree(camBufferPrev);
        camBuffer = NULL;
        camBufferPrev = NULL;
        camExit();
        return;
    }

    memset(camBuffer, 0, CAM_BUF_SIZE);
    memset(camBufferPrev, 0, CAM_BUF_SIZE);

    CAMU_Activate(SELECT_OUT1);
    CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
    CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30);

    svcCreateEvent(&camEvent, RESET_ONESHOT);
    CAMU_SetReceiving(&camEvent, camBuffer, SELECT_OUT1, CAM_BUF_SIZE, (s16)CAM_W);
    CAMU_StartCapture(SELECT_OUT1);

    kameraAcik = true;
}

void kameraKapat(void) {
    if (!kameraAcik) return;

    CAMU_StopCapture(SELECT_OUT1);
    CAMU_Activate(SELECT_NONE);
    svcCloseHandle(camEvent);

    if (camBuffer) linearFree(camBuffer);
    if (camBufferPrev) linearFree(camBufferPrev);

    camBuffer = NULL;
    camBufferPrev = NULL;

    camExit();
    kameraAcik = false;
}

void kameraFrameCek(u8* fb, bool ustEkran) {
    if (!kameraAcik || !camBuffer || !fb) return;
    if (svcWaitSynchronization(camEvent, 0) != 0) return;

    int outW = CAM_W / 2;
    int outH = CAM_H / 2;
    int startX = (400 - outW) / 2;
    int startY = (240 - outH) / 2;

    for (int sy = 0; sy < CAM_H; sy += 2) {
        for (int sx = 0; sx < CAM_W; sx += 2) {
            u16 px = camBuffer[sy * CAM_W + sx];

            u8 r = (u8)(((px >> 11) & 0x1F) << 3);
            u8 g = (u8)(((px >> 5) & 0x3F) << 2);
            u8 b = (u8)((px & 0x1F) << 3);

            int tx = startX + sx / 2;
            int ty = startY + sy / 2;

            pixelDraw(fb, tx, ty, r, g, b, true);
        }
    }

    svcClearEvent(camEvent);
    CAMU_SetReceiving(&camEvent, camBuffer, SELECT_OUT1, CAM_BUF_SIZE, (s16)CAM_W);
}

void drawKenwoodShell(u8* fb) {
    gfxRect(fb, 16, 18, 368, 202, 8, 9, 12, true);
    gfxRect(fb, 22, 24, 356, 190, 20, 22, 28, true);
    gfxRect(fb, 34, 44, 332, 138, 2, 8, 18, true);

    gfxLine(fb, 34, 44, 366, 44, 70, 80, 95, true);
    gfxLine(fb, 34, 182, 366, 182, 70, 80, 95, true);
    gfxLine(fb, 34, 44, 34, 182, 70, 80, 95, true);
    gfxLine(fb, 366, 44, 366, 182, 70, 80, 95, true);

    textDraw(fb, "KENWOOD", 46, 30, 170, 190, 210, true);
    textDraw(fb, "DSP DIGITAL SIGNAL PROCESSOR", 142, 30, 80, 120, 140, true);

    gfxRect(fb, 40, 188, 320, 18, 6, 12, 18, true);
}

void drawPerspectiveTunnel(u8* fb, int amp, u8 r, u8 g, u8 b) {
    int cx = 200;
    int topY = 54;
    int botY = 170;

    // Draw perspective side rails with segment-by-segment depth shading (fog)
    for (int i = 0; i < 15; i++) {
        int leftTop = 58 + i * 8;
        int rightTop = 342 - i * 8;
        int leftBot = 116 + i * 4;
        int rightBot = 284 - i * 4;

        int segments = 8;
        for (int s = 0; s < segments; s++) {
            float fStart = (float)s / segments;
            float fEnd = (float)(s + 1) / segments;
            
            int y0 = topY + (int)(fStart * (botY - topY));
            int y1 = topY + (int)(fEnd * (botY - topY));
            int xl0 = leftTop + (int)(fStart * (leftBot - leftTop));
            int xl1 = leftTop + (int)(fEnd * (leftBot - leftTop));
            int xr0 = rightTop + (int)(fStart * (rightBot - rightTop));
            int xr1 = rightTop + (int)(fEnd * (rightBot - rightTop));

            u8 intensity = 50 + (205 * (s + 1)) / segments;
            gfxLine(fb, xl0, y0, xl1, y1, (r * intensity) / (255 * 4), (g * intensity) / (255 * 3), (b * intensity) / (255 * 2), true);
            gfxLine(fb, xr0, y0, xr1, y1, (r * intensity) / (255 * 4), (g * intensity) / (255 * 3), (b * intensity) / (255 * 2), true);
        }
    }

    // Dynamic horizontal rings shifting forward for a smooth flight animation
    float timeOffset = fmodf(animasyonFaz * 0.35f, 1.0f);
    for (int row = 0; row < 12; row++) {
        float fRow = (float)row + timeOffset;
        int y = topY + (int)(fRow * 9.5f);
        int spread = 150 - (int)(fRow * 8.5f);
        int pulse = abs((int)(sin_fast(animasyonFaz * 0.7f + row) * amp / 3));

        float depthFactor = fRow / 12.0f;
        int br = (int)((70 + pulse) * (0.2f + 0.8f * depthFactor));
        if (br > 255) br = 255;
        if (br < 0) br = 0;

        for (int x = cx - spread; x <= cx + spread; x += 12) {
            gfxRect(fb, x, y, 7, 3, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
    }

    gfxCircle(fb, cx, 142, 34 + amp / 12, r, g, b, true);
    gfxCircle(fb, cx, 142, 20 + amp / 18, r / 2, g / 2, b / 2, true);

    for (int i = 0; i < 12; i++) {
        float a = animasyonFaz * 0.12f + i * 0.52f;
        int x = cx + (int)(cos_fast(a) * (34.0f + amp / 20.0f));
        int y = 142 + (int)(sin_fast(a) * (12.0f + amp / 30.0f));
        gfxGlowPixel(fb, x, y, 255, 80, 40, true);
    }
}

void drawSpectrumLayer(u8* fb, int layer, int tip, int stil, int mod, int renkModu, float slider3D, bool sagGoz) {
    u8 cR, cG, cB;
    getColorByMode(renkModu, &cR, &cG, &cB);

    int shift = sagGoz ? (int)(slider3D * 9.0f) : -(int)(slider3D * 9.0f);
    int amp = viz_anlik_genlik;
    if (amp < 4) amp = 4;

    int cx = 200 + shift;
    int cy = 120;
    int barSayisi = 44;

    for (int i = 0; i < barSayisi; i++) {
        int x = 0;
        int y = 0;
        int h = 0;

        float faz = i * 0.29f + animasyonFaz + layer * 0.75f;

        if (mod == 2) {
            faz += sin_fast(i * 0.14f + layer) * 2.0f;
        }

        int sVal = (int)(sin_fast(faz) * amp * 0.75f);

        switch (tip) {
            case 0:
                x = 48 + i * 7 + shift;
                y = 168;
                h = abs(sVal) + 4;
                break;

            case 1:
                x = cx - 154 + i * 7;
                y = cy;
                h = sVal;
                break;

            case 2:
                x = 48 + i * 7 + shift;
                y = 168;
                h = (int)((1.0f - fabsf(i - 22) / 22.0f) * amp) + abs(sVal) / 3;
                break;

            case 3:
                x = cx + (i - 22) * 6;
                y = cy - abs(sVal);
                h = abs(sVal) * 2 + 2;
                break;

            case 4:
                x = 48 + i * 7 + shift;
                y = 56 + ((frameSayaci * 2 + i * 13 + abs(sVal)) % 110);
                h = 10 + abs(sVal) / 4;
                break;

            case 5:
                x = 48 + i * 7 + shift;
                y = cy + sVal / 2;
                h = 5;
                gfxGlowPixel(fb, x, cy - sVal / 2, cR / 2, cG / 2, cB / 2, true);
                break;

            case 6:
                x = cx + sVal;
                y = 58 + i * 3;
                h = 4;
                break;

            case 7:
                x = (i < 22) ? (50 + i * 6 + shift) : (350 - (i - 22) * 6 + shift);
                y = 164;
                h = abs(sVal) + 5;
                break;

            case 8:
                x = cx + (int)(cos_fast(faz * 1.4f) * 110.0f);
                y = cy + sVal / 2;
                h = 6;
                break;

            case 9:
                x = 48 + i * 7 + shift;
                y = 166 - abs((int)(sin_fast(i * 7.7f + animasyonFaz * 2.0f) * amp / 2));
                h = 3;
                break;

            case 10:
                x = 42 + i * 7 + shift;
                y = 56 + (i % 12) * 8 + (int)(sin_fast(animasyonFaz + i) * 5);
                h = 4;
                break;

            case 11:
                x = 60 + i * 6 + shift;
                y = 150 + (int)(sin_fast(animasyonFaz * 0.9f + i * 0.35f) * 18);
                h = 5;
                break;

            case 12:
                x = cx + (int)(cos_fast(faz * 2.0f) * (40 + i * 2));
                y = cy + (int)(sin_fast(faz * 1.4f) * (20 + i));
                h = 3;
                break;

            default:
                x = 50 + i * 7 + shift;
                y = 162 - abs(sVal) / 2;
                h = 4 + abs(sVal) / 6;
                break;
        }

        if (mod == 1 && tip != 1 && tip != 3) {
            y = 220 - y;
            h = -h;
        }

        int adimY = (h >= 0) ? 1 : -1;
        int mutlakH = abs(h);

        for (int k = 0; k < mutlakH; k++) {
            int drawY = y - k * adimY;

            if (x < 38 || x > 362 || drawY < 48 || drawY > 178) continue;

            switch (stil) {
                case 0:
                    gfxGlowPixel(fb, x, drawY, cR, cG, cB, true);
                    break;

                case 1:
                    if (k % 4 != 3) {
                        gfxRect(fb, x, drawY, 3, 1, cR, cG, cB, true);
                    }
                    break;

                case 2:
                    if (k % 6 == 0) {
                        gfxRect(fb, x, drawY, 2, 2, cR, cG, cB, true);
                    }
                    break;

                case 3:
                    if (k >= mutlakH - 2) {
                        gfxRect(fb, x, drawY, 3, 1, 255, 255, 255, true);
                    } else {
                        pixelDraw(fb, x, drawY, cR / 3, cG / 3, cB / 3, true);
                    }
                    break;

                default:
                    if (mutlakH > 0) {
                        gfxGlowPixel(fb, x, drawY, (cR * k) / mutlakH, (cG * k) / mutlakH, (cB * k) / mutlakH, true);
                    }
                    break;
            }
        }

        // Peak hold decay logic
        int peakLimit = 120;
        int curH = mutlakH;
        if (curH > peakLimit) curH = peakLimit;
        
        if (curH > (int)peakHold[i]) {
            peakHold[i] = (float)curH;
            peakHoldDecay[i] = 15;
        } else {
            if (peakHoldDecay[i] > 0) {
                peakHoldDecay[i]--;
            } else {
                peakHold[i] -= 0.4f;
                if (peakHold[i] < 0) peakHold[i] = 0;
            }
        }
        
        // Draw peak hold dot
        int peakY = y - (int)peakHold[i] * adimY;
        if (x >= 38 && x <= 362 && peakY >= 48 && peakY <= 178) {
            gfxRect(fb, x, peakY, 2, 1, 255, 180, 20, true);
        }
    }
}

void drawDolphinAnim(u8* fb, u8 r, u8 g, u8 b) {
    int baseX = 70 + (frameSayaci * 2) % 270;
    int baseY = 100 + (int)(sin_fast(animasyonFaz * 0.7f) * 26);

    // Pulsing background sun
    int sunR = 15 + viz_anlik_genlik / 6;
    gfxCircle(fb, 280, 75, sunR, 255, 180, 20, true);
    gfxCircle(fb, 280, 75, sunR - 3, 255, 230, 60, true);

    gfxLine(fb, baseX, baseY, baseX + 26, baseY - 8, r, g, b, true);
    gfxLine(fb, baseX + 26, baseY - 8, baseX + 52, baseY, r, g, b, true);
    gfxLine(fb, baseX, baseY, baseX + 26, baseY + 8, r / 2, g / 2, b / 2, true);
    gfxLine(fb, baseX + 26, baseY + 8, baseX + 52, baseY, r / 2, g / 2, b / 2, true);

    gfxLine(fb, baseX + 36, baseY - 2, baseX + 48, baseY - 18, r, g, b, true);
    gfxLine(fb, baseX + 36, baseY - 2, baseX + 48, baseY + 12, r / 2, g / 2, b / 2, true);

    gfxLine(fb, baseX - 8, baseY, baseX - 22, baseY - 10, r, g, b, true);
    gfxLine(fb, baseX - 8, baseY, baseX - 22, baseY + 10, r, g, b, true);

    gfxGlowPixel(fb, baseX + 43, baseY - 2, 255, 255, 255, true);

    // Water wave splashes when dolphin hits water
    if (baseY > 115) {
        pixelDraw(fb, baseX + 10, 138, 0, 170, 255, true);
        pixelDraw(fb, baseX + 20, 136, 255, 255, 255, true);
        pixelDraw(fb, baseX + 30, 138, 0, 170, 255, true);
    }

    for (int i = 0; i < 12; i++) {
        int wx = 50 + i * 28;
        int wy = 140 + (int)(sin_fast(animasyonFaz + i * 0.5f) * 9);
        gfxLine(fb, wx, wy, wx + 18, wy + 4, 0, 170, 255, true);
    }
}

void drawStarfieldAnim(u8* fb, u8 r, u8 g, u8 b) {
    int cx = 200;
    int cy = 112;

    for (int i = 0; i < 70; i++) {
        int seed = i * 37 + coverSeed;
        float a = (seed % 628) / 100.0f;
        int speed = 1 + (seed % 5);
        int dist = (frameSayaci * speed + seed) % 170;

        int x = cx + (int)(cos_fast(a) * dist);
        int y = cy + (int)(sin_fast(a) * dist * 0.55f);

        int prevDist = dist - speed * 2;
        if (prevDist < 0) prevDist = 0;
        int px = cx + (int)(cos_fast(a) * prevDist);
        int py = cy + (int)(sin_fast(a) * prevDist * 0.55f);

        if (x > 38 && x < 362 && y > 48 && y < 178) {
            gfxLine(fb, px, py, x, y, r, g, b, true);
        }
    }

    textDrawCentered(fb, "SPACE VFD", 184, r, g, b, true);
}

void drawTapeAnim(u8* fb, u8 r, u8 g, u8 b) {
    int y = 112;

    // Detailed cassette body outlines
    gfxLine(fb, 80, 60, 320, 60, r / 3, g / 3, b / 3, true);
    gfxLine(fb, 80, 160, 320, 160, r / 3, g / 3, b / 3, true);
    gfxLine(fb, 80, 60, 80, 160, r / 3, g / 3, b / 3, true);
    gfxLine(fb, 320, 60, 320, 160, r / 3, g / 3, b / 3, true);

    gfxRect(fb, 92, 78, 216, 72, 8, 15, 24, true);
    gfxLine(fb, 92, 78, 308, 78, r / 2, g / 2, b / 2, true);
    gfxLine(fb, 92, 150, 308, 150, r / 2, g / 2, b / 2, true);

    int rot = frameSayaci % 32;

    unsigned long anlikKonum = mp3Dosyasi ? (unsigned long)ftell(mp3Dosyasi) : 0;
    float progress = tahminiToplamBoyut ? ((float)anlikKonum / tahminiToplamBoyut) : 0.0f;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    // Tape winding progress circles
    int radL = 12 + (int)(15.0f * (1.0f - progress));
    int radR = 12 + (int)(15.0f * progress);

    gfxDisc(fb, 145, y, radL, r / 4, g / 4, b / 4, true);
    gfxDisc(fb, 255, y, radR, r / 4, g / 4, b / 4, true);
    gfxCircle(fb, 145, y, radL, r / 2, g / 2, b / 2, true);
    gfxCircle(fb, 255, y, radR, r / 2, g / 2, b / 2, true);

    gfxCircle(fb, 145, y, 11, r, g, b, true);
    gfxCircle(fb, 255, y, 11, r, g, b, true);

    for (int i = 0; i < 6; i++) {
        float a = (i * 1.047f) + (caliyor ? rot * 0.15f : 0.0f);
        gfxLine(fb, 145, y, 145 + (int)(cos_fast(a) * 11), y + (int)(sin_fast(a) * 11), r, g, b, true);
        gfxLine(fb, 255, y, 255 + (int)(cos_fast(-a) * 11), y + (int)(sin_fast(-a) * 11), r, g, b, true);
    }

    gfxLine(fb, 145, y + 22, 255, y + 22, 100, 100, 100, true);

    char countStr[16];
    snprintf(countStr, sizeof(countStr), "COUNT %03d", anlikSaniye);
    textDraw(fb, countStr, 172, 82, 255, 180, 60, true);

    textDrawCentered(fb, "TAPE MONITOR", 184, 255, 180, 60, true);
}

void drawRadarAnim(u8* fb, u8 r, u8 g, u8 b) {
    int cx = 200;
    int cy = 118;

    gfxCircle(fb, cx, cy, 28, r / 3, g / 3, b / 3, true);
    gfxCircle(fb, cx, cy, 56, r / 3, g / 3, b / 3, true);
    gfxCircle(fb, cx, cy, 84, r / 3, g / 3, b / 3, true);

    float a = animasyonFaz * 0.08f;

    // Fading sweep trailing arm lines
    for (int j = 0; j < 12; j++) {
        float sweepA = a - j * 0.06f;
        int brFactor = 255 - j * 20;
        if (brFactor < 0) brFactor = 0;
        
        u8 cr = (r * brFactor) / 255;
        u8 cg = (g * brFactor) / 255;
        u8 cb = (b * brFactor) / 255;
        
        int sx = cx + (int)(cos_fast(sweepA) * 90.0f);
        int sy = cy + (int)(sin_fast(sweepA) * 60.0f);
        gfxLine(fb, cx, cy, sx, sy, cr, cg, cb, true);
    }

    // Audio-reactive blips fading away as radar sweeps
    for (int i = 0; i < 8; i++) {
        float pa = i * 0.77f + coverSeed * 0.001f;
        int px = cx + (int)(cos_fast(pa) * (30 + (i * 9)));
        int py = cy + (int)(sin_fast(pa) * (18 + (i * 5)));
        
        float diff = fmodf(a - pa, 2.0f * M_PI);
        if (diff < 0) diff += 2.0f * M_PI;
        
        float intensity = 0.0f;
        if (diff < M_PI) {
            intensity = 1.0f - (diff / M_PI);
        }
        
        u8 br = (u8)(50.0f + 205.0f * intensity);
        gfxGlowPixel(fb, px, py, (255 * br) / 255, (90 * br) / 255, (40 * br) / 255, true);
    }

    textDrawCentered(fb, "RADAR SCAN", 184, r, g, b, true);
}

void drawOscilloscopeAnim(u8* fb, u8 r, u8 g, u8 b) {
    // Dotted grid background
    for (int gx = 50; gx < 350; gx += 30) {
        for (int gy = 60; gy < 160; gy += 4) {
            pixelDraw(fb, gx, gy, r / 7, g / 7, b / 7, true);
        }
    }
    for (int gy = 60; gy < 160; gy += 20) {
        for (int gx = 50; gx < 350; gx += 4) {
            pixelDraw(fb, gx, gy, r / 7, g / 7, b / 7, true);
        }
    }

    int prevX = 42;
    int prevY1 = 112;
    int prevY2 = 112;

    for (int x = 42; x < 358; x += 3) {
        float t = (x - 42) * 0.06f + animasyonFaz;
        
        // Channel 1: Left (Cyan-ish phosphor trace)
        int y1 = 112 + (int)(sin_fast(t) * (18 + viz_anlik_genlik / 4)) + (int)(sin_fast(t * 2.7f) * 8);
        
        // Channel 2: Right (Amber/yellow phosphor trace)
        int y2 = 112 + (int)(cos_fast(t * 0.8f) * (18 + viz_anlik_genlik / 4)) + (int)(sin_fast(t * 3.3f) * 6);

        gfxLine(fb, prevX, prevY1, x, y1, COL_CYAN_R, COL_CYAN_G, COL_CYAN_B, true);
        gfxLine(fb, prevX, prevY2, x, y2, COL_AMBER_R, COL_AMBER_G, COL_AMBER_B, true);

        prevX = x;
        prevY1 = y1;
        prevY2 = y2;
    }

    textDrawCentered(fb, "OSCILLOSCOPE", 184, r, g, b, true);
}

void drawGeneratedCover(u8* fb) {
    char cleanName[48];

    if (playingPlaylistCount > 0) {
        cleanTrackName(getBasename(playingPlaylist[aktifSarkiIdx].path), cleanName, sizeof(cleanName));
    } else {
        snprintf(cleanName, sizeof(cleanName), "NO TRACK");
    }

    int seed = coverSeed;
    u8 r, g, b;
    getColorByMode(seed % 5, &r, &g, &b);

    gfxRect(fb, 74, 48, 252, 132, 4, 8, 15, true);
    gfxLine(fb, 74, 48, 326, 48, r, g, b, true);
    gfxLine(fb, 74, 180, 326, 180, r, g, b, true);
    gfxLine(fb, 74, 48, 74, 180, r, g, b, true);
    gfxLine(fb, 326, 48, 326, 180, r, g, b, true);

    for (int i = 0; i < 28; i++) {
        int aSeed = seed + i * 91;
        float a = (aSeed % 628) / 100.0f;
        int rad = 18 + (aSeed % 70);
        int x = 200 + (int)(cos_fast(a + animasyonFaz * 0.025f) * rad);
        int y = 112 + (int)(sin_fast(a - animasyonFaz * 0.018f) * (rad / 2));

        gfxCircle(fb, x, y, 4 + (aSeed % 9), r / 2, g / 2, b / 2, true);
    }

    gfxDisc(fb, 200, 112, 35, r / 4, g / 4, b / 4, true);
    gfxCircle(fb, 200, 112, 42, r, g, b, true);
    gfxCircle(fb, 200, 112, 18, 255, 255, 255, true);

    textDrawCentered(fb, cleanName, 188, 230, 245, 255, true);
}

void drawCdTop(u8* fb, int y) {
    int cx = 200;
    gfxDisc(fb, cx, y, 26, 150, 180, 210, true);
    gfxDisc(fb, cx, y, 9, 15, 18, 26, true);
    gfxCircle(fb, cx, y, 26, 0, 220, 255, true);

    // Holo reflection spin
    float rotAngle = cdAnimFrame * 0.22f;
    for (int j = 0; j < 4; j++) {
        float refA = rotAngle + j * 1.57f;
        int rx = cx + (int)(cos_fast(refA) * 25.0f);
        int ry = y + (int)(sin_fast(refA) * 25.0f);
        gfxLine(fb, cx, y, rx, ry, 240, 245, 255, true);
    }
    
    // Glowing insertion slot
    gfxRect(fb, 120, 192, 160, 4, 30, 40, 50, true);
    gfxLine(fb, 120, 192, 280, 192, 0, 180, 255, true);
}

void drawCdBottom(u8* fb, int y) {
    int cx = 160;
    gfxDisc(fb, cx, y, 24, 150, 180, 210, false);
    gfxDisc(fb, cx, y, 8, 15, 18, 26, false);
    gfxCircle(fb, cx, y, 24, 0, 220, 255, false);

    // Holo reflection spin
    float rotAngle = cdAnimFrame * 0.22f;
    for (int j = 0; j < 4; j++) {
        float refA = rotAngle + j * 1.57f;
        int rx = cx + (int)(cos_fast(refA) * 23.0f);
        int ry = y + (int)(sin_fast(refA) * 23.0f);
        gfxLine(fb, cx, y, rx, ry, 240, 245, 255, false);
    }
    
    // Glowing entry slot
    gfxRect(fb, 80, 6, 160, 4, 30, 40, 50, false);
    gfxLine(fb, 80, 6, 240, 6, 0, 180, 255, false);
}

void drawCdInsertTopAnim(u8* fb) {
    if (!cdAnimAktif) return;

    int t = cdAnimFrame;

    if (t < 70) {
        int y = 20 + t * 3;
        drawCdTop(fb, y);

        if (t < 45) {
            textDrawCentered(fb, "DISC LOADING", 184, 255, 120, 40, true);
        }
    }
}

void drawCdInsertBottomAnim(u8* fb) {
    if (!cdAnimAktif) return;

    int t = cdAnimFrame;

    if (t >= 70 && t < 120) {
        int y = -20 + (t - 70) * 4;
        drawCdBottom(fb, y);
        textDrawCentered(fb, "DISC IN", 144, 255, 120, 40, false);
    }

    cdAnimFrame++;

    if (cdAnimFrame > 120) {
        cdAnimAktif = false;
        cdAnimFrame = 0;
    }
}

void vfdQuadMixCizim(u8* fb, float slider3D, bool sagGoz) {
    drawKenwoodShell(fb);

    int amp = viz_anlik_genlik;
    if (amp < 4) amp = 4;

    drawPerspectiveTunnel(fb, amp, 0, 220, 255);

    for (int i = 0; i < QUAD_VIZ_SAYISI; i++) {
        drawSpectrumLayer(fb, i, quadVizTipi[i], quadVizStili[i], quadVizModu[i], quadRenkModu[i], slider3D, sagGoz);
    }

    textDrawCentered(fb, "PEAK HOLD", 188, 0, 255, 255, true);
}

void vfdProsedurelCizim(u8* fb, float slider3D, bool sagGoz) {
    if (coverMode) {
        drawKenwoodShell(fb);
        drawGeneratedCover(fb);
        drawCdInsertTopAnim(fb);
        return;
    }

    if (aktifVizTipi == VIZ_QUAD_MIX_TIPI) {
        vfdQuadMixCizim(fb, slider3D, sagGoz);
        drawCdInsertTopAnim(fb);
        return;
    }

    u8 cR, cG, cB;
    getMColor(&cR, &cG, &cB);

    drawKenwoodShell(fb);
    drawPerspectiveTunnel(fb, viz_anlik_genlik, cR, cG, cB);

    if (aktifVizTipi == 11) {
        drawDolphinAnim(fb, cR, cG, cB);
    } else if (aktifVizTipi == 12) {
        drawStarfieldAnim(fb, cR, cG, cB);
    } else if (aktifVizTipi == 13) {
        drawTapeAnim(fb, cR, cG, cB);
    } else if (aktifVizTipi == 10) {
        drawRadarAnim(fb, cR, cG, cB);
    } else if (aktifVizTipi == 9) {
        drawOscilloscopeAnim(fb, cR, cG, cB);
    } else {
        drawSpectrumLayer(fb, 0, aktifVizTipi, aktifVizStili, aktifVizMod, aktifRenkModu, slider3D, sagGoz);
    }

    textDrawCentered(fb, "PEAK HOLD", 188, 0, 255, 255, true);
    drawCdInsertTopAnim(fb);
}

void drawAnimatedButton(u8* fb, int cx, int cy, int r, bool pressed, u8 cr, u8 cg, u8 cb) {
    int pr = pressed ? r - 2 : r;
    int br = pressed ? 110 : 210;

    gfxDisc(fb, cx, cy, pr, (cr * br) / 255, (cg * br) / 255, (cb * br) / 255, false);
    gfxCircle(fb, cx, cy, pr, 210, 230, 255, false);
}

void drawBottomHeader(u8* fb, const char* title) {
    gfxRect(fb, 0, 0, 320, 240, 18, 20, 24, false);
    gfxRect(fb, 8, 8, 304, 28, 7, 10, 16, false);
    textDraw(fb, "KENWOOD", 16, 18, 160, 185, 210, false);
    textDraw(fb, title, 104, 18, 0, 255, 255, false);
    drawGear(fb, 296, 22, 120, 255, 140, false);
}

void drawFileBrowser(u8* fb) {
    drawBottomHeader(fb, "FILE MANAGER");

    gfxRect(fb, 10, 42, 300, 154, 6, 9, 15, false);

    if (browserEntryCount <= 0) {
        textDrawCentered(fb, "EMPTY DIRECTORY", 100, 255, 100, 60, false);
        return;
    }

    if (fileCursor < fileScroll) fileScroll = fileCursor;
    if (fileCursor >= fileScroll + 8) fileScroll = fileCursor - 7;

    for (int i = 0; i < 8; i++) {
        int idx = fileScroll + i;
        if (idx >= browserEntryCount) break;

        int y = 48 + i * 18;
        char displayName[64];

        if (browserEntries[idx].isDirectory) {
            snprintf(displayName, sizeof(displayName), "<DIR> %s", browserEntries[idx].name);
        } else {
            char cleanName[42];
            cleanTrackName(browserEntries[idx].name, cleanName, sizeof(cleanName));
            snprintf(displayName, sizeof(displayName), "      %s", cleanName);
        }

        if (idx == fileCursor) {
            gfxRect(fb, 14, y - 3, 292, 16, 0, 70, 110, false);
            textDraw(fb, ">", 18, y, 255, 220, 60, false);
            if (browserEntries[idx].isDirectory) {
                textDraw(fb, displayName, 34, y, COL_AMBER_R, COL_AMBER_G, COL_AMBER_B, false);
            } else {
                textDraw(fb, displayName, 34, y, 255, 255, 255, false);
            }
        } else {
            if (browserEntries[idx].isDirectory) {
                textDraw(fb, displayName, 34, y, COL_GRN_R, COL_GRN_G, COL_GRN_B, false);
            } else {
                textDraw(fb, displayName, 34, y, 120, 185, 210, false);
            }
        }
    }

    textDraw(fb, "A PLAY/DIR  X VIS  Y AUTO  B PLAYER", 14, 204, 255, 180, 60, false);
    textDraw(fb, "D-PAD SELECT", 14, 220, 90, 255, 150, false);
}

void drawSettings(u8* fb) {
    drawBottomHeader(fb, "SETTINGS");

    const char* labels[] = {
        "EQ 60HZ",
        "EQ 150HZ",
        "EQ 400HZ",
        "EQ 1KHZ",
        "EQ 3KHZ",
        "EQ 8KHZ",
        "EQ 14KHZ",
        "VOLUME",
        "SPEED",
        "CRT GLOW",
        "COVER MODE",
        "SHUFFLE",
        "REPEAT",
        "AUTO VIZ"
    };

    int values[] = {
        eqBand[0], eqBand[1], eqBand[2], eqBand[3], eqBand[4], eqBand[5], eqBand[6],
        masterVolume, playbackRatePct, crtFosforGucu,
        coverMode ? 1 : 0,
        shuffleMode ? 1 : 0,
        repeatMode,
        otomatikDegistir ? 1 : 0
    };

    int itemCount = 14;
    int start = settingsCursor - 4;
    if (start < 0) start = 0;
    if (start > itemCount - 8) start = itemCount - 8;
    if (start < 0) start = 0;

    gfxRect(fb, 10, 42, 300, 154, 6, 9, 15, false);

    for (int i = 0; i < 8; i++) {
        int idx = start + i;
        if (idx >= itemCount) break;

        int y = 48 + i * 18;

        if (idx == settingsCursor) {
            gfxRect(fb, 14, y - 3, 292, 16, 0, 70, 110, false);
            textDraw(fb, ">", 18, y, 255, 220, 60, false);
        }

        textDraw(fb, labels[idx], 34, y, idx == settingsCursor ? 255 : 130, idx == settingsCursor ? 255 : 190, idx == settingsCursor ? 255 : 210, false);

        char val[18];

        if (idx == 10 || idx == 11 || idx == 13) {
            snprintf(val, sizeof(val), "%s", values[idx] ? "ON" : "OFF");
        } else if (idx == 12) {
            snprintf(val, sizeof(val), "%d", repeatMode);
        } else {
            snprintf(val, sizeof(val), "%d", values[idx]);
        }

        textDraw(fb, val, 238, y, 0, 255, 150, false);
    }

    textDraw(fb, "LEFT/RIGHT CHANGE  B BACK", 14, 204, 255, 180, 60, false);
    textDraw(fb, "ZL/ZR PRESET", 14, 220, 90, 255, 150, false);
}

void drawBottomDeck(u8* fb) {
    drawBottomHeader(fb, "PLAYER");

    gfxRect(fb, 8, 42, 304, 106, 7, 10, 16, false);
    gfxRect(fb, 14, 48, 292, 94, 12, 18, 28, false);

    if (playingPlaylistCount > 0) {
        char cleanName[48];
        cleanTrackName(getBasename(playingPlaylist[aktifSarkiIdx].path), cleanName, sizeof(cleanName));
        textDraw(fb, cleanName, 18, 54, 230, 240, 255, false);
    } else {
        textDraw(fb, "NO TRACK", 18, 54, 255, 90, 60, false);
    }

    textDraw(fb, eqPresetNames[eqPresetIdx], 18, 74, 110, 255, 120, false);

    if (coverMode) textDraw(fb, "COVER", 78, 74, 255, 180, 60, false);
    if (otomatikDegistir) textDraw(fb, "AUTO", 128, 74, 255, 130, 40, false);
    if (kameraAcik) textDraw(fb, "CAM", 176, 74, 0, 255, 100, false);
    if (rotateModFull) textDraw(fb, "ROT", 216, 74, 255, 200, 0, false);

    char info[42];
    snprintf(info, sizeof(info), "VOL %d  SPD %d", masterVolume, playbackRatePct);
    textDraw(fb, info, 18, 94, 130, 220, 255, false);

    unsigned long anlikKonum = mp3Dosyasi ? (unsigned long)ftell(mp3Dosyasi) : 0;
    float sProgress = tahminiToplamBoyut ? ((float)anlikKonum / tahminiToplamBoyut) : 0.0f;

    gfxRect(fb, 18, 126, 284, 8, 6, 8, 12, false);

    if (sProgress > 0.0f) {
        gfxRect(fb, 18, 126, (int)(284 * sProgress), 8, 0, 190, 255, false);
    }

    anlikSaniye = (int)(anlikKonum / 16000);

    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", anlikSaniye / 60, anlikSaniye % 60);
    textDraw(fb, timeStr, 244, 94, 0, 255, 255, false);

    drawAnimatedButton(fb, 38, 200, 20, buttons.rew, 50, 90, 130);
    drawAnimatedButton(fb, 94, 200, 22, buttons.prev, 50, 90, 130);
    drawAnimatedButton(fb, 160, 200, 29, buttons.play, 0, 150, 255);
    drawAnimatedButton(fb, 226, 200, 22, buttons.next, 50, 90, 130);
    drawAnimatedButton(fb, 282, 200, 20, buttons.ff, 50, 90, 130);

    textDraw(fb, "<<", 31, 197, 255, 255, 255, false);
    textDraw(fb, "<", 91, 197, 255, 255, 255, false);
    textDraw(fb, caliyor ? "||" : ">", 153, 197, 255, 255, 255, false);
    textDraw(fb, ">", 223, 197, 255, 255, 255, false);
    textDraw(fb, ">>", 275, 197, 255, 255, 255, false);

    drawCdInsertBottomAnim(fb);
}

void renderFrame(void) {
    float slider3D = osGet3DSliderState();

    frameSayaci++;
    animasyonFaz += caliyor ? 0.35f : 0.05f;
    globalPulse = sin_fast(animasyonFaz * 0.5f);

    if (otomatikDegistir && caliyor && frameSayaci % 300 == 0) {
        rastgeleAnimasyonSec();
    }

    u8* fbTopL = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    crtEkranTemizle(fbTopL, true, kameraAcik);

    if (kameraAcik) {
        kameraFrameCek(fbTopL, true);
    } else {
        vfdProsedurelCizim(fbTopL, slider3D, false);
    }

    drawScanlines(fbTopL, true);

    u8* fbTopR = gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
    crtEkranTemizle(fbTopR, true, kameraAcik);

    if (kameraAcik) {
        kameraFrameCek(fbTopR, true);
    } else {
        vfdProsedurelCizim(fbTopR, slider3D, true);
    }

    drawScanlines(fbTopR, true);

    u8* fbBot = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);

    if (uiMode == UI_FILE_BROWSER) {
        drawFileBrowser(fbBot);
    } else if (uiMode == UI_SETTINGS) {
        drawSettings(fbBot);
    } else {
        drawBottomDeck(fbBot);
    }

    drawScanlines(fbBot, false);

    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

void adjustSetting(int delta) {
    if (settingsCursor >= 0 && settingsCursor <= 6) {
        eqBand[settingsCursor] += delta * 5;
        if (eqBand[settingsCursor] < 50) eqBand[settingsCursor] = 50;
        if (eqBand[settingsCursor] > 160) eqBand[settingsCursor] = 160;
    } else if (settingsCursor == 7) {
        masterVolume += delta * 5;
        if (masterVolume < 0) masterVolume = 0;
        if (masterVolume > 150) masterVolume = 150;
    } else if (settingsCursor == 8) {
        playbackRatePct += delta * 5;
        if (playbackRatePct < 60) playbackRatePct = 60;
        if (playbackRatePct > 140) playbackRatePct = 140;
        sesKanaliniYenidenBaslat();
    } else if (settingsCursor == 9) {
        crtFosforGucu += delta * 10;
        if (crtFosforGucu < 0) crtFosforGucu = 0;
        if (crtFosforGucu > 250) crtFosforGucu = 250;
    } else if (settingsCursor == 10) {
        coverMode = !coverMode;
    } else if (settingsCursor == 11) {
        shuffleMode = !shuffleMode;
    } else if (settingsCursor == 12) {
        repeatMode += delta;
        if (repeatMode < 0) repeatMode = 2;
        if (repeatMode > 2) repeatMode = 0;
    } else if (settingsCursor == 13) {
        otomatikDegistir = !otomatikDegistir;
    }
}

int main(void) {
    srand((unsigned int)time(NULL));

    osSetSpeedupEnable(true);

    gfxInitDefault();
    gfxSet3D(true);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);
    gfxSetDoubleBuffering(GFX_TOP, true);

    ndspInit();
    romfsInit();
    trigInit();

    applyEqPreset(eqPresetIdx);

    strncpy(currentDir, "sdmc:/", sizeof(currentDir));
    loadDirectory(currentDir);

    playingPlaylistCount = 0;
    for (int i = 0; i < browserEntryCount; i++) {
        if (!browserEntries[i].isDirectory && playingPlaylistCount < MAKS_SARKI) {
            strncpy(playingPlaylist[playingPlaylistCount].path, browserEntries[i].path, sizeof(playingPlaylist[playingPlaylistCount].path));
            playingPlaylistCount++;
        }
    }

    if (playingPlaylistCount == 0) {
        strncpy(currentDir, "romfs:/", sizeof(currentDir));
        loadDirectory(currentDir);

        playingPlaylistCount = 0;
        for (int i = 0; i < browserEntryCount; i++) {
            if (!browserEntries[i].isDirectory && playingPlaylistCount < MAKS_SARKI) {
                strncpy(playingPlaylist[playingPlaylistCount].path, browserEntries[i].path, sizeof(playingPlaylist[playingPlaylistCount].path));
                playingPlaylistCount++;
            }
        }
    }

    if (playingPlaylistCount > 0) {
        mp3Buffer = (unsigned char*)malloc(BUFFER_BOYUTU);
        pcmBufferA = (s16*)linearAlloc(HAM_SES_BOYUTU);
        pcmBufferB = (s16*)linearAlloc(HAM_SES_BOYUTU);

        if (pcmBufferA && pcmBufferB) {
            dalgaBlokuA.data_vaddr = (u32*)pcmBufferA;
            dalgaBlokuB.data_vaddr = (u32*)pcmBufferB;
        }
    }

    memset(&buttons, 0, sizeof(buttons));

    startAudioThread();
    rastgeleAnimasyonSec();

    while (aptMainLoop()) {
        hidScanInput();

        u32 kDown = hidKeysDown();
        u32 kUp = hidKeysUp();
        u32 kHeld = hidKeysHeld();

        if (kDown & KEY_START) break;

        if (kDown & KEY_A) {
            if (uiMode == UI_FILE_BROWSER) {
                if (browserEntryCount > 0) {
                    int idx = fileCursor;
                    if (browserEntries[idx].isDirectory) {
                        loadDirectory(browserEntries[idx].path);
                        fileCursor = 0;
                        fileScroll = 0;
                    } else {
                        playingPlaylistCount = 0;
                        int selectedIdxInPlaylist = 0;
                        for (int i = 0; i < browserEntryCount; i++) {
                            if (!browserEntries[i].isDirectory && playingPlaylistCount < MAKS_SARKI) {
                                strncpy(playingPlaylist[playingPlaylistCount].path, browserEntries[i].path, sizeof(playingPlaylist[playingPlaylistCount].path));
                                if (i == idx) {
                                    selectedIdxInPlaylist = playingPlaylistCount;
                                }
                                playingPlaylistCount++;
                            }
                        }
                        
                        if (playingPlaylistCount > 0) {
                            aktifSarkiIdx = selectedIdxInPlaylist;
                            if (sarkiYukle(playingPlaylist[aktifSarkiIdx].path)) {
                                caliyor = true;
                                audioThreadPlaying = true;
                                uiMode = UI_PLAYER;
                            }
                        }
                    }
                }
            } else if (uiMode == UI_SETTINGS) {
                adjustSetting(1);
            } else {
                if (!kameraAcik) kameraBaslat();
                else kameraKapat();
            }
        }

        if (kDown & KEY_B) {
            if (uiMode == UI_SETTINGS) {
                uiMode = UI_PLAYER;
            } else if (uiMode == UI_FILE_BROWSER) {
                uiMode = UI_PLAYER;
            } else {
                rotateModFull = !rotateModFull;
            }
        }

        if (kDown & KEY_SELECT) {
            uiMode = UI_FILE_BROWSER;
        }

        if (kDown & KEY_X) {
            if (uiMode == UI_SETTINGS) {
                coverMode = !coverMode;
            } else {
                rastgeleAnimasyonSec();
            }
        }

        if (kDown & KEY_Y) {
            if (uiMode == UI_PLAYER) {
                otomatikDegistir = !otomatikDegistir;
            } else if (uiMode == UI_FILE_BROWSER) {
                uiMode = UI_SETTINGS;
            }
        }

        if (kDown & KEY_L) {
            if (uiMode == UI_PLAYER) shuffleMode = !shuffleMode;
            else if (uiMode == UI_SETTINGS) adjustSetting(-1);
        }

        if (kDown & KEY_R) {
            if (uiMode == UI_PLAYER) repeatMode = (repeatMode + 1) % 3;
            else if (uiMode == UI_SETTINGS) adjustSetting(1);
        }

        if (kDown & KEY_ZL) {
            eqPresetIdx = (eqPresetIdx - 1 + 6) % 6;
            applyEqPreset(eqPresetIdx);
        }

        if (kDown & KEY_ZR) {
            eqPresetIdx = (eqPresetIdx + 1) % 6;
            applyEqPreset(eqPresetIdx);
        }

        if (uiMode == UI_FILE_BROWSER) {
            if (kDown & KEY_DUP) {
                fileCursor--;
                if (fileCursor < 0) fileCursor = 0;
            }

            if (kDown & KEY_DDOWN) {
                fileCursor++;
                if (fileCursor >= browserEntryCount) fileCursor = browserEntryCount - 1;
            }

            if (kDown & KEY_DLEFT) {
                fileCursor -= 8;
                if (fileCursor < 0) fileCursor = 0;
            }

            if (kDown & KEY_DRIGHT) {
                fileCursor += 8;
                if (fileCursor >= browserEntryCount) fileCursor = browserEntryCount - 1;
            }
        } else if (uiMode == UI_SETTINGS) {
            if (kDown & KEY_DUP) {
                settingsCursor--;
                if (settingsCursor < 0) settingsCursor = 13;
            }

            if (kDown & KEY_DDOWN) {
                settingsCursor++;
                if (settingsCursor > 13) settingsCursor = 0;
            }

            if (kDown & KEY_DLEFT) adjustSetting(-1);
            if (kDown & KEY_DRIGHT) adjustSetting(1);
        } else {
            if (kDown & KEY_DUP) {
                crtFosforGucu += 20;
                if (crtFosforGucu > 250) crtFosforGucu = 250;
            }

            if (kDown & KEY_DDOWN) {
                crtFosforGucu -= 20;
                if (crtFosforGucu < 0) crtFosforGucu = 0;
            }

            if ((kHeld & KEY_DLEFT) && mp3Dosyasi && frameSayaci % 10 == 0) {
                sarkiZamaniniDegistir(-5);
            }

            if ((kHeld & KEY_DRIGHT) && mp3Dosyasi && frameSayaci % 10 == 0) {
                sarkiZamaniniDegistir(5);
            }
        }

        touchPosition touch;

        if (kDown & KEY_TOUCH) {
            hidTouchRead(&touch);

            int tx = touch.px;
            int ty = touch.py;

            if (tx >= 276 && tx <= 314 && ty >= 4 && ty <= 40) {
                uiMode = UI_SETTINGS;
            }

            if (uiMode == UI_FILE_BROWSER) {
                if (tx >= 14 && tx <= 306 && ty >= 45 && ty <= 190) {
                    int row = (ty - 48) / 18;
                    int idx = fileScroll + row;

                    if (idx >= 0 && idx < browserEntryCount) {
                        fileCursor = idx;
                        if (browserEntries[idx].isDirectory) {
                            loadDirectory(browserEntries[idx].path);
                            fileCursor = 0;
                            fileScroll = 0;
                        } else {
                            playingPlaylistCount = 0;
                            int selectedIdxInPlaylist = 0;
                            for (int i = 0; i < browserEntryCount; i++) {
                                if (!browserEntries[i].isDirectory && playingPlaylistCount < MAKS_SARKI) {
                                    strncpy(playingPlaylist[playingPlaylistCount].path, browserEntries[i].path, sizeof(playingPlaylist[playingPlaylistCount].path));
                                    if (i == idx) {
                                        selectedIdxInPlaylist = playingPlaylistCount;
                                    }
                                    playingPlaylistCount++;
                                }
                            }
                            
                            if (playingPlaylistCount > 0) {
                                aktifSarkiIdx = selectedIdxInPlaylist;
                                if (sarkiYukle(playingPlaylist[aktifSarkiIdx].path)) {
                                    caliyor = true;
                                    audioThreadPlaying = true;
                                    uiMode = UI_PLAYER;
                                }
                            }
                        }
                    }
                }
            } else if (uiMode == UI_SETTINGS) {
                if (tx >= 14 && tx <= 306 && ty >= 45 && ty <= 190) {
                    int row = (ty - 48) / 18;
                    int start = settingsCursor - 4;
                    if (start < 0) start = 0;
                    if (start > 6) start = 6;

                    int idx = start + row;
                    if (idx >= 0 && idx <= 13) {
                        settingsCursor = idx;

                        if (tx < 160) adjustSetting(-1);
                        else adjustSetting(1);
                    }
                }
            } else {
                if (tx >= 132 && tx <= 188 && ty >= 172 && ty <= 230) buttons.play = true;
                if (tx >= 72 && tx <= 116 && ty >= 178 && ty <= 222) buttons.prev = true;
                if (tx >= 204 && tx <= 248 && ty >= 178 && ty <= 222) buttons.next = true;
                if (tx >= 18 && tx <= 58 && ty >= 180 && ty <= 220) buttons.rew = true;
                if (tx >= 262 && tx <= 302 && ty >= 180 && ty <= 220) buttons.ff = true;

                if (tx >= 18 && tx <= 302 && ty >= 126 && ty <= 134 && mp3Dosyasi) {
                    float rel = (float)(tx - 18) / 284.0f;

                    if (rel < 0.0f) rel = 0.0f;
                    if (rel > 1.0f) rel = 1.0f;

                    fseek(mp3Dosyasi, (long)(tahminiToplamBoyut * rel), SEEK_SET);
                    mad_stream_finish(&Stream);
                    mad_stream_init(&Stream);
                }
            }
        }

        if (kUp & KEY_TOUCH && uiMode == UI_PLAYER) {
            if (buttons.play) {
                if (!mp3Dosyasi && playingPlaylistCount > 0) {
                    sarkiYukle(playingPlaylist[aktifSarkiIdx].path);
                }

                caliyor = !caliyor;
                audioThreadPlaying = caliyor;

                cdAnimAktif = true;
                cdAnimFrame = 0;

                if (caliyor) {
                    sesKanaliniYenidenBaslat();
                }

                buttons.play = false;
            }

            if (buttons.prev) {
                if (playingPlaylistCount > 0) {
                    aktifSarkiIdx = (aktifSarkiIdx - 1 + playingPlaylistCount) % playingPlaylistCount;
                    sarkiYukle(playingPlaylist[aktifSarkiIdx].path);
                    caliyor = true;
                    audioThreadPlaying = true;
                }

                buttons.prev = false;
            }

            if (buttons.next) {
                if (playingPlaylistCount > 0) {
                    aktifSarkiIdx = (aktifSarkiIdx + 1) % playingPlaylistCount;
                    sarkiYukle(playingPlaylist[aktifSarkiIdx].path);
                    caliyor = true;
                    audioThreadPlaying = true;
                }

                buttons.next = false;
            }

            if (buttons.rew) {
                sarkiZamaniniDegistir(-15);
                buttons.rew = false;
            }

            if (buttons.ff) {
                sarkiZamaniniDegistir(15);
                buttons.ff = false;
            }
        }

        if (audioThread && !audioThreadRunning) {
            stopAudioThread();
            startAudioThread();
        }

        renderFrame();
        svcSleepThread(1000000ULL);
    }

    caliyor = false;
    audioThreadPlaying = false;

    stopAudioThread();

    ndspChnReset(SES_KANALI);
    svcSleepThread(100000ULL);

    if (mp3Dosyasi) fclose(mp3Dosyasi);
    if (mp3Buffer) free(mp3Buffer);
    if (pcmBufferA) linearFree(pcmBufferA);
    if (pcmBufferB) linearFree(pcmBufferB);

    kameraKapat();

    mad_synth_finish(&Synth);
    mad_frame_finish(&Frame);
    mad_stream_finish(&Stream);

    romfsExit();
    ndspExit();
    gfxExit();

    return 0;
}