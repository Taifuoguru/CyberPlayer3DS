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

#define DR_FLAC_IMPLEMENTATION
#include "third_party/dr_flac.h"
#include "third_party/stb_vorbis.c"

#define SES_KANALI              0
#define BUFFER_BOYUTU           (4096 * 4)
#define HAM_SES_BOYUTU          (1152 * 2 * 2 * 3)
#define MAKS_SARKI              2048
#define CAM_W                   400
#define CAM_H                   240
#define CAM_BUF_SIZE            (CAM_W * CAM_H * 2)
#define TRIG_TABLE_SIZE         8192

#define VIZ_TIP_SAYISI          96
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
    UI_STARTUP = 0,
    UI_FILE_BROWSER = 1,
    UI_PLAYER = 2,
    UI_SETTINGS = 3,
    UI_SHUTDOWN = 4
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

typedef enum {
    AUDIO_NONE = 0,
    AUDIO_MP3,
    AUDIO_WAV,
    AUDIO_FLAC,
    AUDIO_OGG,
    AUDIO_AAC,
    AUDIO_M4A,
    AUDIO_OPUS,
    AUDIO_UNSUPPORTED
} AudioFormat;

BrowserEntry browserEntries[MAKS_SARKI];
int browserEntryCount = 0;

PlaylistEntry playingPlaylist[MAKS_SARKI];
volatile int playingPlaylistCount = 0;

volatile int aktifSarkiIdx = 0;
int fileCursor = 0;
int fileScroll = 0;
char currentDir[512] = "sdmc:/";
char savedBrowserDir[512] = "sdmc:/";
bool audioSearchActive = false;
bool audioSearchRunning = false;
char audioSearchQueue[256][512];
int audioSearchQueueCount = 0;
int browserDirSeen = 0;
int browserFileSeen = 0;
int browserAudioSeen = 0;
int browserUnsupportedSeen = 0;
int browserSearchDirs = 0;

bool mouseMode = false;
bool geigerMode = true;
bool matrixLogMode = true;
bool cameraFlareMode = true;
bool cyberSnakeMode = false;
bool lowBatterySim = false;
bool tapeJamActive = false;
bool lidAnimActive = false;
int lidAnimFrame = 0;
int scanlineSpacing = 3;
int scanlineStrength = 28;
int matrixRainOffset = 0;
int geigerSpikeFrames = 0;
int micNoiseLevel = 0;
int fakeBatteryPct = 77;
int fakeThermalLevel = 36;
int uiBeepCooldown = 0;
int cameraFailFrames = 0;
float gyroParallaxX = 0.0f;
float gyroParallaxY = 0.0f;

s16* beepPcm = NULL;
ndspWaveBuf beepWave;
int beepPhase = 0;
aptHookCookie aptCookie;
char statusMessage[96] = "SDMC READY";
int statusMessageFrames = 0;

float peakHold[44] = {0};
int peakHoldDecay[44] = {0};

FILE* mp3Dosyasi = NULL;
AudioFormat aktifSesFormati = AUDIO_NONE;
bool madAktif = false;
drflac* flacDecoder = NULL;
stb_vorbis* vorbisDecoder = NULL;
stb_vorbis_info vorbisInfo;
unsigned long wavDataStart = 0;
unsigned long wavDataSize = 0;
unsigned short wavChannels = 0;
unsigned short wavBitsPerSample = 0;
unsigned int wavSampleRate = 44100;
unsigned char* mp3Buffer = NULL;
s16* pcmBufferA = NULL;
s16* pcmBufferB = NULL;
s16 codecDecodeTemp[4096];
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
UiMode uiMode = UI_STARTUP;
UiMode settingsReturnMode = UI_FILE_BROWSER;

bool holdMode = false;
int stationStaticFrames = 0;
int pendingTrackDelta = 0;
int startupFrame = 0;
int shutdownFrame = 0;

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
const char* vfdIsimleri2[] = {"SCOPE", "EQ", "PULSE", "WAVE", "MTRIX", "RADAR", "GRID", "HELIX", "CORE", "BEAM", "DOLPH", "STAR", "TAPE", "ORBIT", "HWAY", "DRIVE", "AQUA", "CITY", "METER", "LASER", "NEON", "REACT", "TURBO", "SONAR", "MIDNT", "VECTOR", "CHROME", "BLADE", "DRIFT", "METRO", "SATRN", "PLASM", "FUSION", "SKY", "BASS", "XRAY", "GRID2", "CRUISE", "ZEN", "PROTO", "DASH", "RALLY", "LUNAR", "SOLAR", "NOVA", "PRISM", "HORIZ", "VORTX", "PANEL", "RETRO", "BOOST", "AUROR", "NIGHT", "TRON", "CIRCT", "WARP", "VINYL", "SYNTH", "MONO", "CHIP", "PHASE", "DEPTH", "GHOST", "ELITE"};
char anlikVizIsmi[64] = "KENWOOD VFD v8.0";

static inline float sin_fast(float ang);
static inline float cos_fast(float ang);

const char* getBasename(const char* path);
bool isPathADirectory(const char* path);
void setStatusMessage(const char* msg);
void triggerUiBeep(int tone);
void triggerLidAnimation(void);
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
void drawKenwoodShell(u8* fb);

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
    float baseRate = (aktifSesFormati == AUDIO_WAV && wavSampleRate > 0) ? (float)wavSampleRate : 44100.0f;
    ndspChnSetRate(SES_KANALI, baseRate * ((float)playbackRatePct / 100.0f));

    memset(sesMiksaji, 0, sizeof(sesMiksaji));
    sesMiksaji[0] = 1.0f;
    sesMiksaji[1] = 1.0f;
    ndspChnSetMix(SES_KANALI, sesMiksaji);
}

bool sesBufferlariniHazirla(void) {
    if (!mp3Buffer) {
        mp3Buffer = (unsigned char*)malloc(BUFFER_BOYUTU);
    }

    if (!pcmBufferA) {
        pcmBufferA = (s16*)linearAlloc(HAM_SES_BOYUTU);
    }

    if (!pcmBufferB) {
        pcmBufferB = (s16*)linearAlloc(HAM_SES_BOYUTU);
    }

    if (!mp3Buffer || !pcmBufferA || !pcmBufferB) {
        setStatusMessage("AUDIO BUFFER FAILED");
        return false;
    }

    memset(&dalgaBlokuA, 0, sizeof(dalgaBlokuA));
    memset(&dalgaBlokuB, 0, sizeof(dalgaBlokuB));

    dalgaBlokuA.data_vaddr = (u32*)pcmBufferA;
    dalgaBlokuB.data_vaddr = (u32*)pcmBufferB;
    dalgaBlokuA.status = NDSP_WBUF_FREE;
    dalgaBlokuB.status = NDSP_WBUF_FREE;
    hangiBufferA = true;

    return true;
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

void setStatusMessage(const char* msg) {
    if (!msg) return;
    strncpy(statusMessage, msg, sizeof(statusMessage) - 1);
    statusMessage[sizeof(statusMessage) - 1] = '\0';
    statusMessageFrames = 180;
}

void initBeepEngine(void) {
    if (!beepPcm) beepPcm = (s16*)linearAlloc(2048 * sizeof(s16));
    if (!beepPcm) return;

    memset(&beepWave, 0, sizeof(beepWave));
    beepWave.data_vaddr = (u32*)beepPcm;
    beepWave.nsamples = 1024;
    beepWave.status = NDSP_WBUF_FREE;

    ndspChnSetFormat(1, NDSP_FORMAT_MONO_PCM16);
    ndspChnSetInterp(1, NDSP_INTERP_NONE);
    ndspChnSetRate(1, 32728.0f);
    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = 0.22f;
    mix[1] = 0.22f;
    ndspChnSetMix(1, mix);
}

void triggerUiBeep(int tone) {
    if (!beepPcm || uiBeepCooldown > 0) return;

    int period = 20 + (tone % 5) * 7;
    for (int i = 0; i < 1024; i++) {
        int env = 1024 - i;
        int wave = ((i + beepPhase) % period) < (period / 2) ? 1 : -1;
        beepPcm[i] = (s16)(wave * env * 9);
    }
    beepPhase += 37 + tone;

    DSP_FlushDataCache(beepPcm, 1024 * sizeof(s16));
    beepWave.nsamples = 1024;
    beepWave.status = NDSP_WBUF_FREE;
    ndspChnWaveBufAdd(1, &beepWave);
    uiBeepCooldown = 7;
}

void triggerLidAnimation(void) {
    lidAnimActive = true;
    lidAnimFrame = 0;
}

void aptEventHook(APT_HookType hook, void* param) {
    (void)param;
    if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONRESTORE) {
        triggerLidAnimation();
    }
}
bool extEquals(const char* ext, const char* value) {
    if (!ext || !value) return false;

    while (*ext && *value) {
        char a = *ext;
        char b = *value;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
        ext++;
        value++;
    }

    return *ext == '\0' && *value == '\0';
}

const char* getCleanExtension(const char* name) {
    const char* ext = strrchr(name, '.');
    if (!ext) return NULL;
    return ext;
}

AudioFormat audioFormatFromName(const char* name) {
    const char* ext = getCleanExtension(name);
    if (!ext) return AUDIO_NONE;

    if (extEquals(ext, ".mp3")) return AUDIO_MP3;
    if (extEquals(ext, ".wav")) return AUDIO_WAV;
    if (extEquals(ext, ".flac")) return AUDIO_FLAC;
    if (extEquals(ext, ".ogg")) return AUDIO_OGG;
    if (extEquals(ext, ".aac")) return AUDIO_AAC;
    if (extEquals(ext, ".m4a")) return AUDIO_M4A;
    if (extEquals(ext, ".opus")) return AUDIO_OPUS;

    return AUDIO_NONE;
}

AudioFormat audioFormatFromHeader(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return AUDIO_NONE;

    u8 b[16];
    size_t n = fread(b, 1, sizeof(b), f);
    fclose(f);

    if (n >= 3 && b[0] == 'I' && b[1] == 'D' && b[2] == '3') return AUDIO_MP3;
    if (n >= 2 && b[0] == 0xFF && (b[1] & 0xE0) == 0xE0) return AUDIO_MP3;
    if (n >= 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WAVE", 4) == 0) return AUDIO_WAV;
    if (n >= 4 && memcmp(b, "fLaC", 4) == 0) return AUDIO_FLAC;
    if (n >= 4 && memcmp(b, "OggS", 4) == 0) return AUDIO_OGG;
    if (n >= 2 && b[0] == 0xFF && (b[1] & 0xF6) == 0xF0) return AUDIO_AAC;
    if (n >= 12 && memcmp(b + 4, "ftyp", 4) == 0) return AUDIO_M4A;

    return AUDIO_NONE;
}

AudioFormat audioFormatFromFile(const char* path, const char* name) {
    AudioFormat fmt = audioFormatFromName(name ? name : path);
    if (fmt != AUDIO_NONE) return fmt;
    return audioFormatFromHeader(path);
}

bool isAudioFileName(const char* name) {
    return audioFormatFromName(name) != AUDIO_NONE;
}

bool isPlayableAudioName(const char* name) {
    AudioFormat fmt = audioFormatFromName(name);
    return fmt == AUDIO_MP3 || fmt == AUDIO_WAV || fmt == AUDIO_FLAC || fmt == AUDIO_OGG;
}

bool isPlayableAudioFile(const char* path, const char* name) {
    AudioFormat fmt = audioFormatFromFile(path, name);
    return fmt == AUDIO_MP3 || fmt == AUDIO_WAV || fmt == AUDIO_FLAC || fmt == AUDIO_OGG;
}

bool aktifSesHazir(void) {
    if (aktifSesFormati == AUDIO_MP3 || aktifSesFormati == AUDIO_WAV) return mp3Dosyasi != NULL;
    if (aktifSesFormati == AUDIO_FLAC) return flacDecoder != NULL;
    if (aktifSesFormati == AUDIO_OGG) return vorbisDecoder != NULL;
    return false;
}

u16 readLE16(FILE* f) {
    u8 b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    return (u16)(b[0] | (b[1] << 8));
}

u32 readLE32(FILE* f) {
    u8 b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return (u32)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

bool wavHeaderOku(FILE* f) {
    char id[5];
    id[4] = '\0';

    fseek(f, 0, SEEK_SET);
    if (fread(id, 1, 4, f) != 4 || memcmp(id, "RIFF", 4) != 0) return false;
    readLE32(f);
    if (fread(id, 1, 4, f) != 4 || memcmp(id, "WAVE", 4) != 0) return false;

    bool fmtFound = false;
    bool dataFound = false;
    unsigned short audioFormat = 0;

    while (!dataFound && fread(id, 1, 4, f) == 4) {
        u32 chunkSize = readLE32(f);
        long chunkStart = ftell(f);

        if (memcmp(id, "fmt ", 4) == 0) {
            audioFormat = readLE16(f);
            wavChannels = readLE16(f);
            wavSampleRate = readLE32(f);
            readLE32(f);
            readLE16(f);
            wavBitsPerSample = readLE16(f);
            fmtFound = true;
        } else if (memcmp(id, "data", 4) == 0) {
            wavDataStart = (unsigned long)ftell(f);
            wavDataSize = chunkSize;
            dataFound = true;
            break;
        }

        fseek(f, chunkStart + chunkSize + (chunkSize & 1), SEEK_SET);
    }

    if (!fmtFound || !dataFound) return false;
    if (audioFormat != 1 || (wavChannels != 1 && wavChannels != 2) || wavBitsPerSample != 16) return false;

    fseek(f, wavDataStart, SEEK_SET);
    return true;
}

void loadDirectory(const char* path) {
    audioSearchActive = false;
    char normalized[512];
    if (!path || path[0] == '\0') path = "sdmc:/";

    strncpy(normalized, path, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';

    if (strcmp(normalized, "sdmc:") == 0) {
        strncpy(normalized, "sdmc:/", sizeof(normalized) - 1);
        normalized[sizeof(normalized) - 1] = '\0';
    }

    int nlen = strlen(normalized);
    if (nlen > 0 && normalized[nlen - 1] != '/') {
        strncat(normalized, "/", sizeof(normalized) - strlen(normalized) - 1);
    }

    DIR* dir = opendir(normalized);
    
    if (!dir) {
        browserEntryCount = 0;
        browserDirSeen = 0;
        browserFileSeen = 0;
        browserAudioSeen = 0;
        browserUnsupportedSeen = 0;
        setStatusMessage("DIRECTORY OPEN FAILED");
        return;
    }
    
    browserEntryCount = 0;
    browserDirSeen = 0;
    browserFileSeen = 0;
    browserAudioSeen = 0;
    browserUnsupportedSeen = 0;
    strncpy(currentDir, normalized, sizeof(currentDir) - 1);
    currentDir[sizeof(currentDir) - 1] = '\0';
    
    int len = strlen(currentDir);
    if (len > 0 && currentDir[len - 1] != '/' && strcmp(currentDir, "sdmc:") != 0) {
        if (len < sizeof(currentDir) - 2) {
            currentDir[len] = '/';
            currentDir[len + 1] = '\0';
        }
    }
    
    bool isRoot = (strcmp(currentDir, "sdmc:/") == 0 || strcmp(currentDir, "sdmc:") == 0);
    
    if (!isRoot) {
        browserEntries[browserEntryCount].isDirectory = true;
        strncpy(browserEntries[browserEntryCount].name, "..", sizeof(browserEntries[browserEntryCount].name) - 1);
        browserEntries[browserEntryCount].name[sizeof(browserEntries[browserEntryCount].name) - 1] = '\0';
        
        char parent[512];
        strncpy(parent, currentDir, sizeof(parent));
        int plen = strlen(parent);
        if (plen > 0 && parent[plen - 1] == '/') {
            parent[plen - 1] = '\0';
        }
        
        char* lastSlash = strrchr(parent, '/');
        if (lastSlash) {
            if (lastSlash == parent + 5 && strncmp(parent, "sdmc:", 5) == 0) {
                *(lastSlash + 1) = '\0';
            } else {
                *lastSlash = '\0';
            }
        } else {
            strncpy(parent, "sdmc:/", sizeof(parent));
        }
        
        snprintf(browserEntries[browserEntryCount].path, sizeof(browserEntries[browserEntryCount].path), "%s", parent);
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
            browserDirSeen++;
            browserEntries[browserEntryCount].isDirectory = true;
            snprintf(browserEntries[browserEntryCount].name, sizeof(browserEntries[browserEntryCount].name), "%s", ent->d_name);
            snprintf(browserEntries[browserEntryCount].path, sizeof(browserEntries[browserEntryCount].path), "%s", fullPath);
            browserEntryCount++;
        } else {
            browserFileSeen++;
            AudioFormat fmt = audioFormatFromFile(fullPath, ent->d_name);

            if (fmt != AUDIO_NONE) {
                browserAudioSeen++;
                if (fmt == AUDIO_UNSUPPORTED) browserUnsupportedSeen++;
            }

            browserEntries[browserEntryCount].isDirectory = false;
            snprintf(browserEntries[browserEntryCount].name, sizeof(browserEntries[browserEntryCount].name), "%s", ent->d_name);
            snprintf(browserEntries[browserEntryCount].path, sizeof(browserEntries[browserEntryCount].path), "%s", fullPath);
            browserEntryCount++;
        }
    }
    
    closedir(dir);

    char msg[96];
    snprintf(msg, sizeof(msg), "DIR %d  FILE %d  AUDIO %d", browserDirSeen, browserFileSeen, browserAudioSeen);
    setStatusMessage(msg);
}

void addAudioSearchHit(const char* fullPath, const char* name, AudioFormat fmt) {
    if (browserEntryCount >= MAKS_SARKI) return;
    if (fmt == AUDIO_NONE) return;

    browserAudioSeen++;
    if (fmt == AUDIO_AAC || fmt == AUDIO_M4A || fmt == AUDIO_OPUS || fmt == AUDIO_UNSUPPORTED) {
        browserUnsupportedSeen++;
    }

    browserEntries[browserEntryCount].isDirectory = false;
    snprintf(browserEntries[browserEntryCount].name, sizeof(browserEntries[browserEntryCount].name), "%s", name);
    snprintf(browserEntries[browserEntryCount].path, sizeof(browserEntries[browserEntryCount].path), "%s", fullPath);
    browserEntryCount++;
}

void scanAudioRecursive(const char* dirPath, int depth) {
    if (!dirPath || depth > 16 || browserEntryCount >= MAKS_SARKI) return;

    DIR* dir = opendir(dirPath);
    if (!dir) return;

    browserSearchDirs++;

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL && browserEntryCount < MAKS_SARKI) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        char fullPath[512];
        size_t dirLen = strlen(dirPath);
        size_t nameLen = strlen(ent->d_name);
        if (dirLen + nameLen >= sizeof(fullPath)) continue;
        memcpy(fullPath, dirPath, dirLen);
        memcpy(fullPath + dirLen, ent->d_name, nameLen + 1);

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
            browserDirSeen++;
            int len = strlen(fullPath);
            if (len < (int)sizeof(fullPath) - 2 && fullPath[len - 1] != '/') {
                fullPath[len] = '/';
                fullPath[len + 1] = '\0';
            }
            scanAudioRecursive(fullPath, depth + 1);
        } else {
            browserFileSeen++;
            AudioFormat fmt = audioFormatFromFile(fullPath, ent->d_name);
            addAudioSearchHit(fullPath, ent->d_name, fmt);
        }
    }

    closedir(dir);
}

void audioSearchQueuePush(const char* path) {
    if (!path || audioSearchQueueCount >= 256) return;
    snprintf(audioSearchQueue[audioSearchQueueCount], sizeof(audioSearchQueue[audioSearchQueueCount]), "%s", path);
    audioSearchQueueCount++;
}

bool audioSearchQueuePop(char* outPath, int outSize) {
    if (audioSearchQueueCount <= 0) return false;

    audioSearchQueueCount--;
    snprintf(outPath, outSize, "%s", audioSearchQueue[audioSearchQueueCount]);
    return true;
}

void processAudioSearchStep(int maxDirs) {
    if (!audioSearchRunning) return;

    char dirPath[512];
    int processed = 0;

    while (processed < maxDirs && audioSearchQueuePop(dirPath, sizeof(dirPath))) {
        DIR* dir = opendir(dirPath);
        if (!dir) {
            processed++;
            continue;
        }

        browserSearchDirs++;

        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL && browserEntryCount < MAKS_SARKI) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

            char fullPath[512];
            size_t dirLen = strlen(dirPath);
            size_t nameLen = strlen(ent->d_name);
            if (dirLen + nameLen >= sizeof(fullPath)) continue;
            memcpy(fullPath, dirPath, dirLen);
            memcpy(fullPath + dirLen, ent->d_name, nameLen + 1);

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
                browserDirSeen++;
                int len = strlen(fullPath);
                if (len < (int)sizeof(fullPath) - 2 && fullPath[len - 1] != '/') {
                    fullPath[len] = '/';
                    fullPath[len + 1] = '\0';
                }
                audioSearchQueuePush(fullPath);
            } else {
                browserFileSeen++;
                AudioFormat fmt = audioFormatFromFile(fullPath, ent->d_name);
                addAudioSearchHit(fullPath, ent->d_name, fmt);
            }
        }

        closedir(dir);
        processed++;
    }

    if (audioSearchQueueCount <= 0 || browserEntryCount >= MAKS_SARKI) {
        audioSearchRunning = false;
        char msg[96];
        snprintf(msg, sizeof(msg), "SEARCH DONE %d AUDIO %d FILE", browserAudioSeen, browserFileSeen);
        setStatusMessage(msg);
    } else if ((frameSayaci % 20) == 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "SCANNING... %d AUDIO %d DIR", browserAudioSeen, browserSearchDirs);
        setStatusMessage(msg);
    }
}

void searchAudioFilesOnSd(void) {
    snprintf(savedBrowserDir, sizeof(savedBrowserDir), "%s", currentDir);
    audioSearchActive = true;
    audioSearchRunning = true;
    audioSearchQueueCount = 0;
    browserEntryCount = 0;
    browserDirSeen = 0;
    browserFileSeen = 0;
    browserAudioSeen = 0;
    browserUnsupportedSeen = 0;
    browserSearchDirs = 0;
    fileCursor = 0;
    fileScroll = 0;
    snprintf(currentDir, sizeof(currentDir), "AUDIO SEARCH");

    audioSearchQueuePush("sdmc:/");
    setStatusMessage("SCANNING SD AUDIO...");
}

void aktifSesKapat(void) {
    if (mp3Dosyasi) {
        fclose(mp3Dosyasi);
        mp3Dosyasi = NULL;
    }

    if (madAktif) {
        mad_synth_finish(&Synth);
        mad_frame_finish(&Frame);
        mad_stream_finish(&Stream);
        madAktif = false;
    }

    if (flacDecoder) {
        drflac_close(flacDecoder);
        flacDecoder = NULL;
    }

    if (vorbisDecoder) {
        stb_vorbis_close(vorbisDecoder);
        vorbisDecoder = NULL;
    }

    aktifSesFormati = AUDIO_NONE;
}

bool sarkiYukle(const char* dosyaAdi) {
    if (!dosyaAdi) return false;

    if (!sesBufferlariniHazirla()) return false;

    audioThreadPlaying = false;
    caliyor = false;
    svcSleepThread(3000000ULL);

    AudioFormat fmt = audioFormatFromFile(dosyaAdi, dosyaAdi);

    if (fmt == AUDIO_AAC || fmt == AUDIO_M4A || fmt == AUDIO_OPUS || fmt == AUDIO_UNSUPPORTED) {
        setStatusMessage("AAC/M4A/OPUS DECODER NOT BUILT");
        return false;
    }

    if (fmt == AUDIO_NONE) {
        setStatusMessage("NOT AN AUDIO FILE");
        return false;
    }

    aktifSesKapat();
    aktifSesFormati = fmt;

    if (aktifSesFormati == AUDIO_MP3) {
        mp3Dosyasi = fopen(dosyaAdi, "rb");
        if (!mp3Dosyasi) {
            aktifSesKapat();
            setStatusMessage("FILE OPEN FAILED");
            return false;
        }

        fseek(mp3Dosyasi, 0, SEEK_END);
        tahminiToplamBoyut = ftell(mp3Dosyasi);
        fseek(mp3Dosyasi, 0, SEEK_SET);

        mad_stream_init(&Stream);
        mad_frame_init(&Frame);
        mad_synth_init(&Synth);
        madAktif = true;
        wavSampleRate = 44100;
    } else if (aktifSesFormati == AUDIO_WAV) {
        mp3Dosyasi = fopen(dosyaAdi, "rb");
        if (!mp3Dosyasi) {
            aktifSesKapat();
            setStatusMessage("FILE OPEN FAILED");
            return false;
        }

        fseek(mp3Dosyasi, 0, SEEK_END);
        tahminiToplamBoyut = ftell(mp3Dosyasi);
        fseek(mp3Dosyasi, 0, SEEK_SET);

        if (!wavHeaderOku(mp3Dosyasi)) {
            aktifSesKapat();
            setStatusMessage("WAV MUST BE PCM16");
            return false;
        }
    } else if (aktifSesFormati == AUDIO_FLAC) {
        flacDecoder = drflac_open_file(dosyaAdi, NULL);
        if (!flacDecoder) {
            aktifSesKapat();
            setStatusMessage("FLAC OPEN FAILED");
            return false;
        }

        wavChannels = (unsigned short)flacDecoder->channels;
        wavBitsPerSample = 16;
        wavSampleRate = flacDecoder->sampleRate;
        tahminiToplamBoyut = (unsigned long)flacDecoder->totalPCMFrameCount;
        if (wavChannels < 1 || wavChannels > 2) {
            aktifSesKapat();
            setStatusMessage("FLAC MUST BE MONO/STEREO");
            return false;
        }
    } else if (aktifSesFormati == AUDIO_OGG) {
        int err = 0;
        vorbisDecoder = stb_vorbis_open_filename(dosyaAdi, &err, NULL);
        if (!vorbisDecoder) {
            aktifSesKapat();
            setStatusMessage("OGG VORBIS OPEN FAILED");
            return false;
        }

        vorbisInfo = stb_vorbis_get_info(vorbisDecoder);
        wavChannels = (unsigned short)vorbisInfo.channels;
        wavBitsPerSample = 16;
        wavSampleRate = vorbisInfo.sample_rate;
        tahminiToplamBoyut = (unsigned long)stb_vorbis_stream_length_in_samples(vorbisDecoder);
        if (wavChannels < 1 || wavChannels > 2) {
            aktifSesKapat();
            setStatusMessage("OGG MUST BE MONO/STEREO");
            return false;
        }
    }

    sesKanaliniYenidenBaslat();
    viz_anlik_genlik = 0;

    coverSeed = hashString(dosyaAdi);
    cdAnimAktif = true;
    cdAnimFrame = 0;
    if (aktifSesFormati == AUDIO_WAV) setStatusMessage("WAV PCM16 LOADED");
    else if (aktifSesFormati == AUDIO_FLAC) setStatusMessage("FLAC LOADED");
    else if (aktifSesFormati == AUDIO_OGG) setStatusMessage("OGG VORBIS LOADED");
    else setStatusMessage("MP3 LOADED");

    return true;
}

void sarkiZamaniniDegistir(int saniyeFarki) {
    if (!aktifSesHazir()) return;

    if (aktifSesFormati == AUDIO_FLAC) {
        drflac_uint64 cur = flacDecoder->currentPCMFrame;
        drflac_uint64 target = cur + (drflac_uint64)((long long)saniyeFarki * (long long)wavSampleRate);
        if (saniyeFarki < 0 && cur < (drflac_uint64)(-saniyeFarki * (int)wavSampleRate)) target = 0;
        if (target > flacDecoder->totalPCMFrameCount) target = flacDecoder->totalPCMFrameCount;
        drflac_seek_to_pcm_frame(flacDecoder, target);
        return;
    }

    if (aktifSesFormati == AUDIO_OGG) {
        unsigned int cur = stb_vorbis_get_sample_offset(vorbisDecoder);
        int delta = saniyeFarki * (int)wavSampleRate;
        unsigned int total = stb_vorbis_stream_length_in_samples(vorbisDecoder);
        unsigned int target = (delta < 0 && cur < (unsigned int)(-delta)) ? 0 : cur + delta;
        if (target > total) target = total;
        stb_vorbis_seek(vorbisDecoder, target);
        return;
    }

    long bytesPerSecond = 16000;
    if (aktifSesFormati == AUDIO_WAV) {
        bytesPerSecond = (long)wavSampleRate * wavChannels * (wavBitsPerSample / 8);
    }

    long yeniPos = ftell(mp3Dosyasi) + (long)(saniyeFarki * bytesPerSecond);
    long minPos = (aktifSesFormati == AUDIO_WAV) ? (long)wavDataStart : 0;
    long maxPos = (aktifSesFormati == AUDIO_WAV) ? (long)(wavDataStart + wavDataSize) : (long)tahminiToplamBoyut;

    if (yeniPos < minPos) yeniPos = minPos;
    if (yeniPos > maxPos - 100) yeniPos = maxPos - 100;

    if (aktifSesFormati == AUDIO_WAV) {
        int blockAlign = wavChannels * (wavBitsPerSample / 8);
        if (blockAlign > 0) yeniPos -= (yeniPos - (long)wavDataStart) % blockAlign;
    }

    fseek(mp3Dosyasi, yeniPos, SEEK_SET);
    if (aktifSesFormati == AUDIO_MP3 && madAktif) {
        mad_stream_finish(&Stream);
        mad_stream_init(&Stream);
    }
}

float sesIlerlemesi01(void) {
    if (!aktifSesHazir()) return 0.0f;

    if (aktifSesFormati == AUDIO_FLAC && flacDecoder && flacDecoder->totalPCMFrameCount > 0) {
        return (float)((double)flacDecoder->currentPCMFrame / (double)flacDecoder->totalPCMFrameCount);
    }

    if (aktifSesFormati == AUDIO_OGG && vorbisDecoder) {
        unsigned int total = stb_vorbis_stream_length_in_samples(vorbisDecoder);
        if (total > 0) return (float)stb_vorbis_get_sample_offset(vorbisDecoder) / (float)total;
    }

    if (aktifSesFormati == AUDIO_WAV && mp3Dosyasi && wavDataSize > 0) {
        long pos = ftell(mp3Dosyasi) - (long)wavDataStart;
        if (pos < 0) pos = 0;
        return (float)pos / (float)wavDataSize;
    }

    if (aktifSesFormati == AUDIO_MP3 && mp3Dosyasi && tahminiToplamBoyut > 0) {
        return (float)ftell(mp3Dosyasi) / (float)tahminiToplamBoyut;
    }

    return 0.0f;
}

int sesAnlikSaniye(void) {
    if (!aktifSesHazir()) return 0;

    if (aktifSesFormati == AUDIO_FLAC && flacDecoder && wavSampleRate > 0) {
        return (int)(flacDecoder->currentPCMFrame / wavSampleRate);
    }

    if (aktifSesFormati == AUDIO_OGG && vorbisDecoder && wavSampleRate > 0) {
        return (int)(stb_vorbis_get_sample_offset(vorbisDecoder) / wavSampleRate);
    }

    if (aktifSesFormati == AUDIO_WAV && mp3Dosyasi) {
        long pos = ftell(mp3Dosyasi) - (long)wavDataStart;
        long bytesPerSecond = (long)wavSampleRate * wavChannels * 2;
        return bytesPerSecond > 0 ? (int)(pos / bytesPerSecond) : 0;
    }

    if (aktifSesFormati == AUDIO_MP3 && mp3Dosyasi) {
        return (int)(ftell(mp3Dosyasi) / 16000);
    }

    return 0;
}

void sesYuzdeyeGit(float rel) {
    if (!aktifSesHazir()) return;
    if (rel < 0.0f) rel = 0.0f;
    if (rel > 1.0f) rel = 1.0f;

    if (aktifSesFormati == AUDIO_FLAC && flacDecoder) {
        drflac_uint64 target = (drflac_uint64)((double)flacDecoder->totalPCMFrameCount * rel);
        drflac_seek_to_pcm_frame(flacDecoder, target);
    } else if (aktifSesFormati == AUDIO_OGG && vorbisDecoder) {
        unsigned int total = stb_vorbis_stream_length_in_samples(vorbisDecoder);
        stb_vorbis_seek(vorbisDecoder, (unsigned int)((float)total * rel));
    } else if (aktifSesFormati == AUDIO_WAV && mp3Dosyasi) {
        long pos = (long)(wavDataStart + wavDataSize * rel);
        int blockAlign = wavChannels * 2;
        if (blockAlign > 0) pos -= (pos - (long)wavDataStart) % blockAlign;
        fseek(mp3Dosyasi, pos, SEEK_SET);
    } else if (aktifSesFormati == AUDIO_MP3 && mp3Dosyasi) {
        fseek(mp3Dosyasi, (long)(tahminiToplamBoyut * rel), SEEK_SET);
        if (madAktif) {
            mad_stream_finish(&Stream);
            mad_stream_init(&Stream);
        }
    }
}

void sonrakiSarkiyaGec(void) {
    if (shuffleMode && playingPlaylistCount > 1) {
        aktifSarkiIdx = rand() % playingPlaylistCount;
        sarkiYukle(playingPlaylist[aktifSarkiIdx].path);
    } else if (repeatMode == 1 && aktifSesHazir()) {
        if (aktifSesFormati == AUDIO_WAV) {
            fseek(mp3Dosyasi, wavDataStart, SEEK_SET);
        } else if (aktifSesFormati == AUDIO_FLAC) {
            drflac_seek_to_pcm_frame(flacDecoder, 0);
        } else if (aktifSesFormati == AUDIO_OGG) {
            stb_vorbis_seek_start(vorbisDecoder);
        } else {
            fseek(mp3Dosyasi, 0, SEEK_SET);
            if (madAktif) {
                mad_stream_finish(&Stream);
                mad_stream_init(&Stream);
            }
        }
    } else if (playingPlaylistCount > 0) {
        aktifSarkiIdx = (aktifSarkiIdx + 1) % playingPlaylistCount;
        sarkiYukle(playingPlaylist[aktifSarkiIdx].path);
    }
}

void audioProducerThread(void* arg) {
    audioThreadRunning = true;

    while (audioThreadRunning) {
        if (!audioThreadPlaying || !aktifSesHazir()) {
            svcSleepThread(1000000ULL);
            continue;
        }

        ndspWaveBuf* ab = hangiBufferA ? &dalgaBlokuA : &dalgaBlokuB;
        s16* ap = hangiBufferA ? pcmBufferA : pcmBufferB;

        if (ab->status == NDSP_WBUF_DONE || ab->status == NDSP_WBUF_FREE) {
            long toplamOrnek = 0;

            if (aktifSesFormati == AUDIO_FLAC || aktifSesFormati == AUDIO_OGG) {
                int channels = wavChannels ? wavChannels : 2;
                int maxFrames = (HAM_SES_BOYUTU / 2) / 2;
                if (maxFrames > (int)(sizeof(codecDecodeTemp) / sizeof(codecDecodeTemp[0]) / channels)) {
                    maxFrames = (int)(sizeof(codecDecodeTemp) / sizeof(codecDecodeTemp[0]) / channels);
                }

                int framesRead = 0;

                if (aktifSesFormati == AUDIO_FLAC) {
                    framesRead = (int)drflac_read_pcm_frames_s16(flacDecoder, maxFrames, codecDecodeTemp);
                } else {
                    framesRead = stb_vorbis_get_samples_short_interleaved(vorbisDecoder, channels, codecDecodeTemp, maxFrames * channels);
                }

                if (framesRead <= 0) {
                    sonrakiSarkiyaGec();
                    continue;
                }

                for (int i = 0; i < framesRead && toplamOrnek + 1 < HAM_SES_BOYUTU / 2; i++) {
                    s16 sL = codecDecodeTemp[i * channels];
                    s16 sR = (channels == 2) ? codecDecodeTemp[i * channels + 1] : sL;

                    sL = audioProcessSample(sL, 0);
                    sR = audioProcessSample(sR, 1);

                    ap[toplamOrnek++] = sL;
                    ap[toplamOrnek++] = sR;

                    if ((i & 511) == 0) {
                        viz_anlik_genlik = (abs(sL) + abs(sR)) / 1800;
                        if (viz_anlik_genlik > 120) viz_anlik_genlik = 120;
                    }
                }

                DSP_FlushDataCache(ap, toplamOrnek * sizeof(s16));
                ab->nsamples = toplamOrnek / 2;
                ab->status = NDSP_WBUF_FREE;
                ndspChnWaveBufAdd(SES_KANALI, ab);
                hangiBufferA = !hangiBufferA;
                svcSleepThread(1000000ULL);
                continue;
            }

            if (aktifSesFormati == AUDIO_WAV) {
                long dataEnd = (long)(wavDataStart + wavDataSize);
                int channels = wavChannels ? wavChannels : 2;
                int blockAlign = channels * 2;

                while (toplamOrnek + 1 < HAM_SES_BOYUTU / 2 && ftell(mp3Dosyasi) + blockAlign <= dataEnd) {
                    s16 sL = (s16)readLE16(mp3Dosyasi);
                    s16 sR = (channels == 2) ? (s16)readLE16(mp3Dosyasi) : sL;

                    sL = audioProcessSample(sL, 0);
                    sR = audioProcessSample(sR, 1);

                    ap[toplamOrnek++] = sL;
                    ap[toplamOrnek++] = sR;

                    if ((toplamOrnek & 2047) == 0) {
                        viz_anlik_genlik = (abs(sL) + abs(sR)) / 1800;
                        if (viz_anlik_genlik > 120) viz_anlik_genlik = 120;
                    }
                }

                if (toplamOrnek == 0) {
                    sonrakiSarkiyaGec();
                    continue;
                }

                DSP_FlushDataCache(ap, toplamOrnek * sizeof(s16));
                ab->nsamples = toplamOrnek / 2;
                ab->status = NDSP_WBUF_FREE;
                ndspChnWaveBufAdd(SES_KANALI, ab);
                hangiBufferA = !hangiBufferA;
                svcSleepThread(1000000ULL);
                continue;
            }

            int kareSayisi = 0;

            while (kareSayisi < 3) {
                if (Stream.buffer == NULL || Stream.error == MAD_ERROR_BUFLEN) {
                    size_t kalanVeri = (Stream.next_frame != NULL) ? (size_t)(Stream.bufend - Stream.next_frame) : 0;

                    if (kalanVeri > 0) {
                        memmove(mp3Buffer, Stream.next_frame, kalanVeri);
                    }

                    size_t okunan = fread(mp3Buffer + kalanVeri, 1, BUFFER_BOYUTU - kalanVeri, mp3Dosyasi);

                    if (okunan == 0) {
                        sonrakiSarkiyaGec();
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
        vfdIsimleri2[rand() % 64],
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
    int gap = scanlineSpacing;
    if (gap < 1) gap = 1;
    int dim = 100 - scanlineStrength;
    if (dim < 5) dim = 5;

    for (int x = 0; x < W; x++) {
        for (int y = 0; y < 240; y += gap) {
            int idx = ((239 - y) + x * 240) * 3;

            if (idx >= 0 && idx + 2 < W * 240 * 3) {
                fb[idx] = (fb[idx] * dim) / 100;
                fb[idx + 1] = (fb[idx + 1] * dim) / 100;
                fb[idx + 2] = (fb[idx + 2] * dim) / 100;
            }
        }
    }
}

void kameraBaslat(void) {
    if (kameraAcik) return;

    if (R_FAILED(camInit())) {
        setStatusMessage("CAMERA INIT FAILED");
        return;
    }

    camBuffer = (u16*)linearAlloc(CAM_BUF_SIZE);
    camBufferPrev = (u16*)linearAlloc(CAM_BUF_SIZE);

    if (!camBuffer || !camBufferPrev) {
        if (camBuffer) linearFree(camBuffer);
        if (camBufferPrev) linearFree(camBufferPrev);
        camBuffer = NULL;
        camBufferPrev = NULL;
        camExit();
        setStatusMessage("CAMERA BUFFER FAILED");
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
    setStatusMessage("CAMERA ONLINE");
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
    setStatusMessage("CAMERA OFF");
}

void kameraFrameCek(u8* fb, bool ustEkran) {
    if (!kameraAcik || !camBuffer || !fb) return;
    bool yeniFrame = (svcWaitSynchronization(camEvent, 0) == 0);

    if (!yeniFrame) {
        cameraFailFrames++;
        drawKenwoodShell(fb);
        textDrawCentered(fb, "CAMERA SIGNAL WAIT", 96, 255, 180, 60, true);
        textDrawCentered(fb, "CHECK 3DS CAMERA ACCESS", 116, 0, 255, 180, true);
        for (int i = 0; i < 18; i++) {
            int y = 54 + i * 7;
            gfxLine(fb, 56, y, 344, y + (int)(sin_fast(frameSayaci * 0.04f + i) * 3), 0, 80, 120, true);
        }
        return;
    }
    cameraFailFrames = 0;

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

            if (cameraFlareMode && ((sx + sy + frameSayaci) % 97) == 0) {
                int bright = r + g + b;
                if (bright > 420) {
                    gfxGlowPixel(fb, tx, ty, 255, 220, 120, true);
                    gfxLine(fb, tx - 4, ty, tx + 4, ty, 255, 200, 80, true);
                }
            }
        }
    }

    if (yeniFrame) {
        svcClearEvent(camEvent);
        CAMU_SetReceiving(&camEvent, camBuffer, SELECT_OUT1, CAM_BUF_SIZE, (s16)CAM_W);
    }
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

    for (int i = 0; i < 8; i++) {
        int x = 52 + i * 38;
        gfxRect(fb, x, 192, 24, 8, 18, 24, 32, true);
        gfxLine(fb, x, 192, x + 23, 192, 70, 85, 96, true);
    }

    gfxCircle(fb, 342, 196, 13, 90, 105, 118, true);
    gfxCircle(fb, 342, 196, 8, 20, 28, 34, true);
    textDraw(fb, "24BIT", 48, 194, 0, 255, 180, true);
    textDraw(fb, "3D", 138, 194, 255, 190, 40, true);
    textDraw(fb, "EQ", 214, 194, 0, 210, 255, true);
    textDraw(fb, "DSP", 284, 194, 255, 80, 60, true);
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
        
        // Draw red 2px peak hold line.
        int peakY = y - (int)peakHold[i] * adimY;
        if (x >= 38 && x <= 362 && peakY >= 48 && peakY <= 178) {
            gfxRect(fb, x, peakY, 3, 2, 255, 40, 25, true);
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

    float progress = sesIlerlemesi01();
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

void drawCyberHighwayAnim(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * 10.0f) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int horizon = 76;

    for (int i = 0; i < 18; i++) {
        float z = (float)((frameSayaci * 3 + i * 18) % 220) / 220.0f;
        int y = horizon + (int)(z * z * 105.0f);
        int half = 8 + (int)(z * 154.0f);
        int lane = 2 + (int)(z * 18.0f);
        u8 br = (u8)(60 + z * 180);

        gfxLine(fb, cx - half, y, cx - half - 22, y + 10, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        gfxLine(fb, cx + half, y, cx + half + 22, y + 10, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        gfxRect(fb, cx - lane, y, lane * 2, 2, 255, 210, 80, true);
    }

    int carY = 158;
    gfxRect(fb, cx - 38, carY, 76, 12, 14, 18, 24, true);
    gfxRect(fb, cx - 25, carY - 11, 50, 12, 30, 45, 58, true);
    gfxRect(fb, cx - 33, carY + 9, 18, 5, 255, 40 + amp, 20, true);
    gfxRect(fb, cx + 15, carY + 9, 18, 5, 255, 40 + amp, 20, true);
    textDrawCentered(fb, "CYBER HIGHWAY 3D", 188, 0, 255, 255, true);
}

void drawDolphin3DAnim(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * 12.0f) * eye;
    int water = 122 + (int)(sin_fast(animasyonFaz * 0.7f) * 4);

    for (int x = 38; x < 362; x += 8) {
        int y = water + (int)(sin_fast(animasyonFaz * 0.28f + x * 0.08f) * 7);
        gfxRect(fb, x, y, 6, 1, 0, 110, 150, true);
        gfxGlowPixel(fb, x + shift / 2, y - 1, 80, 230, 255, true);
    }

    for (int d = 0; d < 3; d++) {
        float phase = animasyonFaz * (0.08f + d * 0.015f) + d * 2.1f;
        int x = 70 + ((int)(frameSayaci * (1 + d)) % 260) + shift * (d + 1) / 3;
        int y = 92 + (int)(sin_fast(phase) * (18 + d * 5));
        int size = 14 + d * 4;

        gfxDisc(fb, x, y, size, r / (d + 1), g / (d + 1), b, true);
        gfxDisc(fb, x + size, y - 4, size / 2, r / 2, g / 2, b, true);
        gfxLine(fb, x - size, y, x - size - 18, y - 9, r, g, b, true);
        gfxLine(fb, x - size, y, x - size - 18, y + 9, r, g, b, true);
        gfxLine(fb, x + 2, y - size, x + 10, y - size - 13, 255, 255, 255, true);
    }

    textDrawCentered(fb, "AQUA DOLPHIN DEPTH", 188, 0, 255, 255, true);
}

void drawCockpitDriveAnim(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int shift = (sagGoz ? 1 : -1) * (int)(slider3D * 8.0f);
    int cx = 200 + shift;
    int amp = viz_anlik_genlik;

    gfxRect(fb, 44, 52, 312, 92, 3, 7, 14, true);
    for (int i = 0; i < 9; i++) {
        int y = 68 + i * 10;
        int spread = 25 + i * 17;
        gfxLine(fb, cx - spread, y, cx - spread - 28, y + 12, 0, 90, 150, true);
        gfxLine(fb, cx + spread, y, cx + spread + 28, y + 12, 0, 90, 150, true);
    }

    gfxCircle(fb, cx - 86, 158, 28, 100, 130, 150, true);
    gfxCircle(fb, cx + 86, 158, 28, 100, 130, 150, true);
    gfxLine(fb, cx - 86, 158, cx - 86 + (int)(cos_fast(animasyonFaz) * 22), 158 - amp / 4, 255, 60, 35, true);
    gfxLine(fb, cx + 86, 158, cx + 86 + (int)(sin_fast(animasyonFaz) * 22), 158 - amp / 5, 255, 190, 40, true);
    gfxRect(fb, cx - 58, 147, 116, 27, 8, 14, 20, true);
    textDraw(fb, "DSP CAR PLAY", cx - 48, 156, 0, 255, 255, true);
    textDrawCentered(fb, "NIGHT DRIVE", 188, 255, 210, 80, true);
}

void drawSpectrumCityAnim(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int shift = (sagGoz ? 1 : -1) * (int)(slider3D * 11.0f);
    int amp = viz_anlik_genlik;

    for (int i = 0; i < 38; i++) {
        int x = 42 + i * 8 + shift;
        int h = 12 + abs((int)(sin_fast(animasyonFaz * 0.6f + i * 0.41f) * (20 + amp / 2)));
        gfxRect(fb, x, 170 - h, 5, h, r / 3, g / 2, b, true);
        if ((i + frameSayaci / 8) % 3 == 0) gfxRect(fb, x + 1, 168 - h, 2, 2, 255, 220, 80, true);
    }

    for (int i = 0; i < 42; i++) {
        int x = 40 + ((i * 37 + frameSayaci * 2) % 320);
        int y = 58 + ((i * 19) % 80);
        gfxGlowPixel(fb, x + shift / 2, y, 80, 230, 255, true);
    }

    textDrawCentered(fb, "SPECTRUM CITY", 188, 0, 255, 255, true);
}

void drawAnalogMeterWall(u8* fb, u8 r, u8 g, u8 b) {
    int amp = viz_anlik_genlik;

    for (int m = 0; m < 4; m++) {
        int x = 54 + m * 78;
        int y = 70;
        gfxRect(fb, x, y, 58, 70, 12, 15, 18, true);
        gfxCircle(fb, x + 29, y + 40, 25, 80, 95, 110, true);
        int needle = -32 + ((amp + m * 17 + (int)(sin_fast(animasyonFaz + m) * 20)) % 65);
        float a = (-90.0f + needle) * (M_PI / 180.0f);
        gfxLine(fb, x + 29, y + 48, x + 29 + (int)(cos_fast(a) * 22), y + 48 + (int)(sin_fast(a) * 22), 255, 50, 30, true);
        textDraw(fb, "VU", x + 22, y + 12, r, g, b, true);
    }

    textDrawCentered(fb, "KENWOOD METER BRIDGE", 188, 255, 210, 80, true);
}


void drawMegaVizBackdrop(u8* fb, int mode, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 9) + 3)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    for (int i = 0; i < 18; i++) {
        int y = 54 + i * 7;
        int pulse = abs((int)(sin_fast(animasyonFaz * 0.18f + i * 0.47f + mode) * (amp + 18)));
        gfxLine(fb, 42 + pulse / 3, y, 358 - pulse / 4, y + (mode % 5), r / 5, g / 5, b / 4, true);
    }
    for (int s = 0; s < 10; s++) {
        float a = animasyonFaz * 0.05f + s * 0.63f + mode;
        int rad = 18 + s * 12 + (amp / 5);
        int x = cx + (int)(cos_fast(a) * rad);
        int y = cy + (int)(sin_fast(a * 0.7f) * (rad / 2));
        gfxGlowPixel(fb, x, y, r, g, b, true);
    }
}

void drawMegaViz20(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 20;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz21(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 21;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz22(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 22;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz23(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 23;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz24(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 24;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz25(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 25;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz26(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 26;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz27(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 27;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz28(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 28;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz29(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 29;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz30(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 30;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz31(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 31;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz32(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 32;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz33(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 33;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz34(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 34;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz35(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 35;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz36(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 36;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz37(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 37;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz38(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 38;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz39(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 39;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz40(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 40;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz41(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 41;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz42(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 42;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz43(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 43;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz44(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 44;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz45(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 45;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz46(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 46;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz47(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 47;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz48(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 48;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz49(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 49;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz50(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 50;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz51(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 51;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz52(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 52;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz53(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 53;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz54(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 54;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz55(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 55;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz56(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 56;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz57(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 57;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz58(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 58;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz59(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 59;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz60(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 60;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz61(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 61;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz62(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 62;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaViz63(u8* fb, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    int mode = 63;
    int eye = sagGoz ? 1 : -1;
    int shift = (int)(slider3D * (float)((mode % 12) + 4)) * eye;
    int amp = viz_anlik_genlik;
    int cx = 200 + shift;
    int cy = 112;
    drawMegaVizBackdrop(fb, mode, r, g, b, slider3D, sagGoz);
    if ((mode % 4) == 0) {
        for (int i = 0; i < 36; i++) {
            float z = (float)((frameSayaci * ((mode % 5) + 1) + i * 23) % 240) / 240.0f;
            int y = 55 + (int)(z * z * 118.0f);
            int spread = 8 + (int)(z * 160.0f);
            u8 br = (u8)(50 + z * 190);
            gfxLine(fb, cx - spread, y, cx - spread - 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
            gfxLine(fb, cx + spread, y, cx + spread + 18, y + 8, (r * br) / 255, (g * br) / 255, (b * br) / 255, true);
        }
        gfxRect(fb, cx - 34, 154, 68, 14, 18, 22, 28, true);
        gfxRect(fb, cx - 18, 144, 36, 11, r / 3, g / 3, b / 2, true);
    } else if ((mode % 4) == 1) {
        for (int i = 0; i < 44; i++) {
            float a = animasyonFaz * (0.04f + (mode % 6) * 0.005f) + i * 0.37f;
            int rad = 18 + i * 3 + (amp / 4);
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.3f) * (20 + i));
            gfxGlowPixel(fb, x, y, (u8)((r + i * 3) & 255), (u8)((g + i * 5) & 255), b, true);
        }
        gfxCircle(fb, cx, cy, 34 + amp / 8, r, g, b, true);
    } else if ((mode % 4) == 2) {
        for (int i = 0; i < 38; i++) {
            int x = 45 + i * 8 + shift;
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.4f + i * 0.29f + mode) * (amp + 22)));
            gfxRect(fb, x, 170 - h, 5, h, r / 2, g, b, true);
            gfxRect(fb, x, 170 - h - 3, 5, 2, 255, 50, 35, true);
        }
        gfxLine(fb, 40, 172, 360, 172, r, g, b, true);
    } else {
        for (int i = 0; i < 24; i++) {
            int baseX = 50 + i * 13;
            int y = 98 + (int)(sin_fast(animasyonFaz * 0.16f + i * 0.42f + mode) * 35);
            gfxLine(fb, baseX + shift, y, baseX + 8 + shift, y - 8 - amp / 10, r, g, b, true);
            gfxLine(fb, baseX + 8 + shift, y - 8 - amp / 10, baseX + 16 + shift, y, r / 2, g / 2, b, true);
        }
        gfxCircle(fb, cx - 70, 148, 24, 255, 180, 40, true);
        gfxCircle(fb, cx + 70, 148, 24, 255, 180, 40, true);
    }
    char label[32];
    snprintf(label, sizeof(label), "MEGA DSP %02d", mode);
    textDrawCentered(fb, label, 188, 0, 255, 255, true);
}

void drawMegaVizMode(u8* fb, int mode, u8 r, u8 g, u8 b, float slider3D, bool sagGoz) {
    switch (mode) {
        case 20: drawMegaViz20(fb, r, g, b, slider3D, sagGoz); break;
        case 21: drawMegaViz21(fb, r, g, b, slider3D, sagGoz); break;
        case 22: drawMegaViz22(fb, r, g, b, slider3D, sagGoz); break;
        case 23: drawMegaViz23(fb, r, g, b, slider3D, sagGoz); break;
        case 24: drawMegaViz24(fb, r, g, b, slider3D, sagGoz); break;
        case 25: drawMegaViz25(fb, r, g, b, slider3D, sagGoz); break;
        case 26: drawMegaViz26(fb, r, g, b, slider3D, sagGoz); break;
        case 27: drawMegaViz27(fb, r, g, b, slider3D, sagGoz); break;
        case 28: drawMegaViz28(fb, r, g, b, slider3D, sagGoz); break;
        case 29: drawMegaViz29(fb, r, g, b, slider3D, sagGoz); break;
        case 30: drawMegaViz30(fb, r, g, b, slider3D, sagGoz); break;
        case 31: drawMegaViz31(fb, r, g, b, slider3D, sagGoz); break;
        case 32: drawMegaViz32(fb, r, g, b, slider3D, sagGoz); break;
        case 33: drawMegaViz33(fb, r, g, b, slider3D, sagGoz); break;
        case 34: drawMegaViz34(fb, r, g, b, slider3D, sagGoz); break;
        case 35: drawMegaViz35(fb, r, g, b, slider3D, sagGoz); break;
        case 36: drawMegaViz36(fb, r, g, b, slider3D, sagGoz); break;
        case 37: drawMegaViz37(fb, r, g, b, slider3D, sagGoz); break;
        case 38: drawMegaViz38(fb, r, g, b, slider3D, sagGoz); break;
        case 39: drawMegaViz39(fb, r, g, b, slider3D, sagGoz); break;
        case 40: drawMegaViz40(fb, r, g, b, slider3D, sagGoz); break;
        case 41: drawMegaViz41(fb, r, g, b, slider3D, sagGoz); break;
        case 42: drawMegaViz42(fb, r, g, b, slider3D, sagGoz); break;
        case 43: drawMegaViz43(fb, r, g, b, slider3D, sagGoz); break;
        case 44: drawMegaViz44(fb, r, g, b, slider3D, sagGoz); break;
        case 45: drawMegaViz45(fb, r, g, b, slider3D, sagGoz); break;
        case 46: drawMegaViz46(fb, r, g, b, slider3D, sagGoz); break;
        case 47: drawMegaViz47(fb, r, g, b, slider3D, sagGoz); break;
        case 48: drawMegaViz48(fb, r, g, b, slider3D, sagGoz); break;
        case 49: drawMegaViz49(fb, r, g, b, slider3D, sagGoz); break;
        case 50: drawMegaViz50(fb, r, g, b, slider3D, sagGoz); break;
        case 51: drawMegaViz51(fb, r, g, b, slider3D, sagGoz); break;
        case 52: drawMegaViz52(fb, r, g, b, slider3D, sagGoz); break;
        case 53: drawMegaViz53(fb, r, g, b, slider3D, sagGoz); break;
        case 54: drawMegaViz54(fb, r, g, b, slider3D, sagGoz); break;
        case 55: drawMegaViz55(fb, r, g, b, slider3D, sagGoz); break;
        case 56: drawMegaViz56(fb, r, g, b, slider3D, sagGoz); break;
        case 57: drawMegaViz57(fb, r, g, b, slider3D, sagGoz); break;
        case 58: drawMegaViz58(fb, r, g, b, slider3D, sagGoz); break;
        case 59: drawMegaViz59(fb, r, g, b, slider3D, sagGoz); break;
        case 60: drawMegaViz60(fb, r, g, b, slider3D, sagGoz); break;
        case 61: drawMegaViz61(fb, r, g, b, slider3D, sagGoz); break;
        case 62: drawMegaViz62(fb, r, g, b, slider3D, sagGoz); break;
        case 63: drawMegaViz63(fb, r, g, b, slider3D, sagGoz); break;
        default: drawSpectrumLayer(fb, 0, mode % 20, aktifVizStili, aktifVizMod, aktifRenkModu, slider3D, sagGoz); break;
    }
}


void drawDeluxeIndustrialBackdrop(u8* fb, int module, bool top) {
    int W = top ? 400 : 320;
    for (int y = 42; y < 190; y += 12) {
        int wob = (int)(sin_fast(animasyonFaz * 0.07f + y * 0.05f + module) * 9);
        gfxLine(fb, 24 + wob, y, W - 24 - wob, y + (module % 5), 0, 40 + module % 80, 50, top);
    }
}

void drawDeluxeFeatureBank001(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 1;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank002(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 2;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank003(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 3;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank004(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 4;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank005(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 5;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank006(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 6;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank007(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 7;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank008(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 8;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank009(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 9;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank010(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 10;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank011(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 11;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank012(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 12;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank013(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 13;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank014(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 14;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank015(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 15;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank016(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 16;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank017(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 17;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank018(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 18;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank019(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 19;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank020(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 20;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank021(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 21;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank022(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 22;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank023(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 23;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank024(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 24;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank025(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 25;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank026(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 26;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank027(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 27;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank028(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 28;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank029(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 29;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank030(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 30;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank031(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 31;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank032(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 32;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank033(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 33;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank034(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 34;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank035(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 35;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank036(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 36;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank037(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 37;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank038(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 38;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank039(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 39;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank040(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 40;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank041(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 41;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank042(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 42;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank043(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 43;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank044(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 44;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank045(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 45;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank046(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 46;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank047(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 47;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank048(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 48;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank049(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 49;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank050(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 50;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank051(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 51;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank052(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 52;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank053(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 53;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank054(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 54;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank055(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 55;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank056(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 56;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank057(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 57;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank058(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 58;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank059(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 59;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank060(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 60;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank061(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 61;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank062(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 62;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank063(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 63;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank064(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 64;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank065(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 65;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank066(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 66;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank067(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 67;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank068(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 68;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank069(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 69;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank070(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 70;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank071(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 71;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank072(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 72;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank073(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 73;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank074(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 74;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank075(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 75;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank076(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 76;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank077(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 77;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank078(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 78;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank079(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 79;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank080(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 80;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank081(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 81;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank082(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 82;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank083(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 83;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank084(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 84;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank085(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 85;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank086(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 86;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank087(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 87;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank088(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 88;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank089(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 89;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank090(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 90;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank091(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 91;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank092(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 92;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank093(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 93;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank094(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 94;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank095(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 95;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank096(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 96;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank097(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 97;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank098(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 98;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank099(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 99;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank100(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 100;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank101(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 101;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank102(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 102;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank103(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 103;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank104(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 104;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank105(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 105;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank106(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 106;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank107(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 107;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank108(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 108;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank109(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 109;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank110(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 110;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank111(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 111;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank112(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 112;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank113(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 113;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank114(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 114;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank115(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 115;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank116(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 116;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank117(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 117;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank118(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 118;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank119(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 119;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank120(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 120;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank121(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 121;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank122(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 122;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank123(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 123;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank124(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 124;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank125(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 125;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank126(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 126;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank127(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 127;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank128(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 128;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank129(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 129;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank130(u8* fb, u8 r, u8 g, u8 b, bool top) {
    int module = 130;
    int W = top ? 400 : 320;
    int amp = viz_anlik_genlik + (module % 17);
    int cx = W / 2;
    int cy = 112 + (module % 9) - 4;
    drawDeluxeIndustrialBackdrop(fb, module, top);
    if ((module % 6) == 0) {
        for (int k = 0; k < 32; k++) {
            float a = animasyonFaz * 0.035f + k * 0.31f + module;
            int rad = 12 + k * 3 + amp / 4;
            int x = cx + (int)(cos_fast(a) * rad);
            int y = cy + (int)(sin_fast(a * 1.4f) * (rad / 2));
            gfxGlowPixel(fb, x, y, r, g, b, top);
        }
        gfxCircle(fb, cx, cy, 24 + amp / 6, r, g, b, top);
    } else if ((module % 6) == 1) {
        for (int x = 32; x < W - 32; x += 9) {
            int h = 8 + abs((int)(sin_fast(animasyonFaz * 0.22f + x * 0.04f + module) * (amp + 20)));
            gfxRect(fb, x, 176 - h, 5, h, r / 2, g, b, top);
            gfxRect(fb, x, 173 - h, 5, 2, 255, 50, 30, top);
        }
    } else if ((module % 6) == 2) {
        for (int row = 0; row < 12; row++) {
            int y = 58 + row * 9;
            int spread = 18 + row * 13 + amp / 3;
            gfxLine(fb, cx - spread, y, cx - spread / 2, y + 6, r / 2, g / 2, b, top);
            gfxLine(fb, cx + spread, y, cx + spread / 2, y + 6, r / 2, g / 2, b, top);
        }
        gfxRect(fb, cx - 38, 154, 76, 13, 18, 20, 24, top);
    } else if ((module % 6) == 3) {
        for (int k = 0; k < 20; k++) {
            int x = 34 + ((k * 41 + frameSayaci * (module % 5 + 1)) % (W - 68));
            int y = 54 + ((k * 23 + module * 7) % 116);
            gfxLine(fb, x - 5, y, x + 5, y, 255, 230, 90, top);
            gfxLine(fb, x, y - 5, x, y + 5, 255, 120, 40, top);
        }
        textDrawCentered(fb, "LASER GRID", 188, r, g, b, top);
    } else if ((module % 6) == 4) {
        for (int k = 0; k < 5; k++) {
            int x = 48 + k * ((W - 96) / 5);
            gfxRect(fb, x, 72, 38, 70, 10, 14, 18, top);
            gfxCircle(fb, x + 19, 114, 18 + (amp + k * 3) % 9, r, g, b, top);
            gfxLine(fb, x + 19, 114, x + 19 + (int)(cos_fast(animasyonFaz + k) * 15), 114 - amp / 5, 255, 70, 40, top);
        }
    } else {
        for (int k = 0; k < 18; k++) {
            int x = 44 + k * ((W - 88) / 18);
            int y = cy + (int)(sin_fast(animasyonFaz * 0.12f + k * 0.7f + module) * 48);
            gfxGlowPixel(fb, x, y, r, g, b, top);
            gfxLine(fb, x, cy, x, y, r / 4, g / 4, b / 4, top);
        }
    }
    char label[32];
    snprintf(label, sizeof(label), "Y2K MODULE %03d", module);
    textDrawCentered(fb, label, 202, 0, 255, 170, top);
}

void drawDeluxeFeatureBank(u8* fb, int module, u8 r, u8 g, u8 b, bool top) {
    switch (module) {
        case 1: drawDeluxeFeatureBank001(fb, r, g, b, top); break;
        case 2: drawDeluxeFeatureBank002(fb, r, g, b, top); break;
        case 3: drawDeluxeFeatureBank003(fb, r, g, b, top); break;
        case 4: drawDeluxeFeatureBank004(fb, r, g, b, top); break;
        case 5: drawDeluxeFeatureBank005(fb, r, g, b, top); break;
        case 6: drawDeluxeFeatureBank006(fb, r, g, b, top); break;
        case 7: drawDeluxeFeatureBank007(fb, r, g, b, top); break;
        case 8: drawDeluxeFeatureBank008(fb, r, g, b, top); break;
        case 9: drawDeluxeFeatureBank009(fb, r, g, b, top); break;
        case 10: drawDeluxeFeatureBank010(fb, r, g, b, top); break;
        case 11: drawDeluxeFeatureBank011(fb, r, g, b, top); break;
        case 12: drawDeluxeFeatureBank012(fb, r, g, b, top); break;
        case 13: drawDeluxeFeatureBank013(fb, r, g, b, top); break;
        case 14: drawDeluxeFeatureBank014(fb, r, g, b, top); break;
        case 15: drawDeluxeFeatureBank015(fb, r, g, b, top); break;
        case 16: drawDeluxeFeatureBank016(fb, r, g, b, top); break;
        case 17: drawDeluxeFeatureBank017(fb, r, g, b, top); break;
        case 18: drawDeluxeFeatureBank018(fb, r, g, b, top); break;
        case 19: drawDeluxeFeatureBank019(fb, r, g, b, top); break;
        case 20: drawDeluxeFeatureBank020(fb, r, g, b, top); break;
        case 21: drawDeluxeFeatureBank021(fb, r, g, b, top); break;
        case 22: drawDeluxeFeatureBank022(fb, r, g, b, top); break;
        case 23: drawDeluxeFeatureBank023(fb, r, g, b, top); break;
        case 24: drawDeluxeFeatureBank024(fb, r, g, b, top); break;
        case 25: drawDeluxeFeatureBank025(fb, r, g, b, top); break;
        case 26: drawDeluxeFeatureBank026(fb, r, g, b, top); break;
        case 27: drawDeluxeFeatureBank027(fb, r, g, b, top); break;
        case 28: drawDeluxeFeatureBank028(fb, r, g, b, top); break;
        case 29: drawDeluxeFeatureBank029(fb, r, g, b, top); break;
        case 30: drawDeluxeFeatureBank030(fb, r, g, b, top); break;
        case 31: drawDeluxeFeatureBank031(fb, r, g, b, top); break;
        case 32: drawDeluxeFeatureBank032(fb, r, g, b, top); break;
        case 33: drawDeluxeFeatureBank033(fb, r, g, b, top); break;
        case 34: drawDeluxeFeatureBank034(fb, r, g, b, top); break;
        case 35: drawDeluxeFeatureBank035(fb, r, g, b, top); break;
        case 36: drawDeluxeFeatureBank036(fb, r, g, b, top); break;
        case 37: drawDeluxeFeatureBank037(fb, r, g, b, top); break;
        case 38: drawDeluxeFeatureBank038(fb, r, g, b, top); break;
        case 39: drawDeluxeFeatureBank039(fb, r, g, b, top); break;
        case 40: drawDeluxeFeatureBank040(fb, r, g, b, top); break;
        case 41: drawDeluxeFeatureBank041(fb, r, g, b, top); break;
        case 42: drawDeluxeFeatureBank042(fb, r, g, b, top); break;
        case 43: drawDeluxeFeatureBank043(fb, r, g, b, top); break;
        case 44: drawDeluxeFeatureBank044(fb, r, g, b, top); break;
        case 45: drawDeluxeFeatureBank045(fb, r, g, b, top); break;
        case 46: drawDeluxeFeatureBank046(fb, r, g, b, top); break;
        case 47: drawDeluxeFeatureBank047(fb, r, g, b, top); break;
        case 48: drawDeluxeFeatureBank048(fb, r, g, b, top); break;
        case 49: drawDeluxeFeatureBank049(fb, r, g, b, top); break;
        case 50: drawDeluxeFeatureBank050(fb, r, g, b, top); break;
        case 51: drawDeluxeFeatureBank051(fb, r, g, b, top); break;
        case 52: drawDeluxeFeatureBank052(fb, r, g, b, top); break;
        case 53: drawDeluxeFeatureBank053(fb, r, g, b, top); break;
        case 54: drawDeluxeFeatureBank054(fb, r, g, b, top); break;
        case 55: drawDeluxeFeatureBank055(fb, r, g, b, top); break;
        case 56: drawDeluxeFeatureBank056(fb, r, g, b, top); break;
        case 57: drawDeluxeFeatureBank057(fb, r, g, b, top); break;
        case 58: drawDeluxeFeatureBank058(fb, r, g, b, top); break;
        case 59: drawDeluxeFeatureBank059(fb, r, g, b, top); break;
        case 60: drawDeluxeFeatureBank060(fb, r, g, b, top); break;
        case 61: drawDeluxeFeatureBank061(fb, r, g, b, top); break;
        case 62: drawDeluxeFeatureBank062(fb, r, g, b, top); break;
        case 63: drawDeluxeFeatureBank063(fb, r, g, b, top); break;
        case 64: drawDeluxeFeatureBank064(fb, r, g, b, top); break;
        case 65: drawDeluxeFeatureBank065(fb, r, g, b, top); break;
        case 66: drawDeluxeFeatureBank066(fb, r, g, b, top); break;
        case 67: drawDeluxeFeatureBank067(fb, r, g, b, top); break;
        case 68: drawDeluxeFeatureBank068(fb, r, g, b, top); break;
        case 69: drawDeluxeFeatureBank069(fb, r, g, b, top); break;
        case 70: drawDeluxeFeatureBank070(fb, r, g, b, top); break;
        case 71: drawDeluxeFeatureBank071(fb, r, g, b, top); break;
        case 72: drawDeluxeFeatureBank072(fb, r, g, b, top); break;
        case 73: drawDeluxeFeatureBank073(fb, r, g, b, top); break;
        case 74: drawDeluxeFeatureBank074(fb, r, g, b, top); break;
        case 75: drawDeluxeFeatureBank075(fb, r, g, b, top); break;
        case 76: drawDeluxeFeatureBank076(fb, r, g, b, top); break;
        case 77: drawDeluxeFeatureBank077(fb, r, g, b, top); break;
        case 78: drawDeluxeFeatureBank078(fb, r, g, b, top); break;
        case 79: drawDeluxeFeatureBank079(fb, r, g, b, top); break;
        case 80: drawDeluxeFeatureBank080(fb, r, g, b, top); break;
        case 81: drawDeluxeFeatureBank081(fb, r, g, b, top); break;
        case 82: drawDeluxeFeatureBank082(fb, r, g, b, top); break;
        case 83: drawDeluxeFeatureBank083(fb, r, g, b, top); break;
        case 84: drawDeluxeFeatureBank084(fb, r, g, b, top); break;
        case 85: drawDeluxeFeatureBank085(fb, r, g, b, top); break;
        case 86: drawDeluxeFeatureBank086(fb, r, g, b, top); break;
        case 87: drawDeluxeFeatureBank087(fb, r, g, b, top); break;
        case 88: drawDeluxeFeatureBank088(fb, r, g, b, top); break;
        case 89: drawDeluxeFeatureBank089(fb, r, g, b, top); break;
        case 90: drawDeluxeFeatureBank090(fb, r, g, b, top); break;
        case 91: drawDeluxeFeatureBank091(fb, r, g, b, top); break;
        case 92: drawDeluxeFeatureBank092(fb, r, g, b, top); break;
        case 93: drawDeluxeFeatureBank093(fb, r, g, b, top); break;
        case 94: drawDeluxeFeatureBank094(fb, r, g, b, top); break;
        case 95: drawDeluxeFeatureBank095(fb, r, g, b, top); break;
        case 96: drawDeluxeFeatureBank096(fb, r, g, b, top); break;
        case 97: drawDeluxeFeatureBank097(fb, r, g, b, top); break;
        case 98: drawDeluxeFeatureBank098(fb, r, g, b, top); break;
        case 99: drawDeluxeFeatureBank099(fb, r, g, b, top); break;
        case 100: drawDeluxeFeatureBank100(fb, r, g, b, top); break;
        case 101: drawDeluxeFeatureBank101(fb, r, g, b, top); break;
        case 102: drawDeluxeFeatureBank102(fb, r, g, b, top); break;
        case 103: drawDeluxeFeatureBank103(fb, r, g, b, top); break;
        case 104: drawDeluxeFeatureBank104(fb, r, g, b, top); break;
        case 105: drawDeluxeFeatureBank105(fb, r, g, b, top); break;
        case 106: drawDeluxeFeatureBank106(fb, r, g, b, top); break;
        case 107: drawDeluxeFeatureBank107(fb, r, g, b, top); break;
        case 108: drawDeluxeFeatureBank108(fb, r, g, b, top); break;
        case 109: drawDeluxeFeatureBank109(fb, r, g, b, top); break;
        case 110: drawDeluxeFeatureBank110(fb, r, g, b, top); break;
        case 111: drawDeluxeFeatureBank111(fb, r, g, b, top); break;
        case 112: drawDeluxeFeatureBank112(fb, r, g, b, top); break;
        case 113: drawDeluxeFeatureBank113(fb, r, g, b, top); break;
        case 114: drawDeluxeFeatureBank114(fb, r, g, b, top); break;
        case 115: drawDeluxeFeatureBank115(fb, r, g, b, top); break;
        case 116: drawDeluxeFeatureBank116(fb, r, g, b, top); break;
        case 117: drawDeluxeFeatureBank117(fb, r, g, b, top); break;
        case 118: drawDeluxeFeatureBank118(fb, r, g, b, top); break;
        case 119: drawDeluxeFeatureBank119(fb, r, g, b, top); break;
        case 120: drawDeluxeFeatureBank120(fb, r, g, b, top); break;
        case 121: drawDeluxeFeatureBank121(fb, r, g, b, top); break;
        case 122: drawDeluxeFeatureBank122(fb, r, g, b, top); break;
        case 123: drawDeluxeFeatureBank123(fb, r, g, b, top); break;
        case 124: drawDeluxeFeatureBank124(fb, r, g, b, top); break;
        case 125: drawDeluxeFeatureBank125(fb, r, g, b, top); break;
        case 126: drawDeluxeFeatureBank126(fb, r, g, b, top); break;
        case 127: drawDeluxeFeatureBank127(fb, r, g, b, top); break;
        case 128: drawDeluxeFeatureBank128(fb, r, g, b, top); break;
        case 129: drawDeluxeFeatureBank129(fb, r, g, b, top); break;
        case 130: drawDeluxeFeatureBank130(fb, r, g, b, top); break;
        default: drawDeluxeFeatureBank001(fb, r, g, b, top); break;
    }
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
    } else if (aktifVizTipi == 15) {
        drawCyberHighwayAnim(fb, cR, cG, cB, slider3D, sagGoz);
    } else if (aktifVizTipi == 16) {
        drawCockpitDriveAnim(fb, cR, cG, cB, slider3D, sagGoz);
    } else if (aktifVizTipi == 17) {
        drawDolphin3DAnim(fb, cR, cG, cB, slider3D, sagGoz);
    } else if (aktifVizTipi == 18) {
        drawSpectrumCityAnim(fb, cR, cG, cB, slider3D, sagGoz);
    } else if (aktifVizTipi == 19) {
        drawAnalogMeterWall(fb, cR, cG, cB);
    } else if (aktifVizTipi >= 64) {
        drawDeluxeFeatureBank(fb, 1 + ((aktifVizTipi - 64) * 4 + (frameSayaci / 300)) % 130, cR, cG, cB, true);
    } else if (aktifVizTipi >= 20) {
        drawMegaVizMode(fb, aktifVizTipi, cR, cG, cB, slider3D, sagGoz);
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

    if (statusMessageFrames > 0) {
        textDraw(fb, statusMessage, 14, 232, 255, 210, 90, false);
    }
}

void drawHoldOverlay(u8* fb, bool top) {
    int w = top ? 400 : 320;
    gfxRect(fb, w - 72, 8, 58, 16, 40, 8, 8, top);
    textDraw(fb, "[HOLD]", w - 66, 13, 255, 80, 50, top);
}

void drawStationStatic(u8* fb, bool top) {
    int w = top ? 400 : 320;

    for (int i = 0; i < 950; i++) {
        int x = rand() % w;
        int y = rand() % 240;
        u8 v = (u8)(70 + rand() % 186);
        pixelDraw(fb, x, y, v, v, v, top);
    }

    for (int y = 0; y < 240; y += 9) {
        u8 v = (u8)(35 + rand() % 90);
        gfxRect(fb, 0, y + (rand() % 3), w, 1, v, v, v, top);
    }

    textDrawCentered(fb, "RANDOM STATION", 104, 255, 255, 255, top);
}

void drawStartup(u8* fb, bool top) {
    int w = top ? 400 : 320;
    gfxRect(fb, 0, 0, w, 240, 2, 6, 8, top);
    textDraw(fb, "CYBERPLAYER/3DS BIOS", 24, 46, 0, 255, 180, top);
    if (startupFrame > 20) textDraw(fb, "SYSTEM BOOTING...", 24, 78, 0, 255, 180, top);
    if (startupFrame > 55) textDraw(fb, "MOUNTING SDMC...", 24, 96, 0, 255, 180, top);
    if (startupFrame > 90) textDraw(fb, "AUDIO CORE ONLINE", 24, 114, 0, 255, 180, top);
    if ((startupFrame / 12) % 2 == 0) textDraw(fb, "_", 24, 138, 0, 255, 180, top);
}

void drawShutdown(u8* fb, bool top) {
    int w = top ? 400 : 320;
    int fade = 255 - shutdownFrame * 5;
    if (fade < 0) fade = 0;

    gfxRect(fb, 0, 0, w, 240, 0, 0, 0, top);
    textDrawCentered(fb, "POWERING OFF...", 112, fade, fade / 2, fade / 4, top);
}

void finishPendingTrackChange(void) {
    if (pendingTrackDelta == 0 || playingPlaylistCount <= 0) return;

    aktifSarkiIdx = (aktifSarkiIdx + pendingTrackDelta + playingPlaylistCount) % playingPlaylistCount;
    pendingTrackDelta = 0;

    if (sarkiYukle(playingPlaylist[aktifSarkiIdx].path)) {
        caliyor = true;
        audioThreadPlaying = true;
    }
}

void requestTrackChange(int delta) {
    if (playingPlaylistCount <= 0 || stationStaticFrames > 0) return;

    pendingTrackDelta = delta;
    stationStaticFrames = 38;
    caliyor = false;
    audioThreadPlaying = false;
}

bool isSdRoot(void) {
    return strcmp(currentDir, "sdmc:/") == 0 || strcmp(currentDir, "sdmc:") == 0;
}

void drawMatrixLogOverlay(u8* fb) {
    if (!matrixLogMode) return;

    const char* logs[] = {
        "KERNEL BYPASS SUCCESS",
        "DMA BUS ONLINE",
        "SDMC SECTOR MAP OK",
        "DSP PIPE STABLE",
        "VFD PHOSPHOR TRACE",
        "APT SLEEP OVERRIDE",
        "AUDIO CORE UNLOCKED",
        "MATRIX LOGGER ACTIVE"
    };

    matrixRainOffset = (matrixRainOffset + 1) & 255;
    for (int i = 0; i < 9; i++) {
        int y = 44 + i * 16;
        int x = 12 + ((matrixRainOffset + i * 29) % 40);
        textDraw(fb, logs[(i + frameSayaci / 30) % 8], x, y, 0, 70 + (i * 15), 45, false);
    }
}

void drawCyberSnakeOverlay(u8* fb) {
    if (!cyberSnakeMode || !holdMode) return;

    gfxRect(fb, 12, 42, 296, 154, 0, 12, 8, false);
    textDraw(fb, "CYBER-SNAKE HOLD MODE", 54, 52, 0, 255, 150, false);

    int headX = 40 + ((frameSayaci * 2) % 220);
    int headY = 112 + (int)(sin_fast(frameSayaci * 0.08f) * 46);

    for (int i = 0; i < 18; i++) {
        int x = headX - i * 8;
        int y = headY + (int)(sin_fast(frameSayaci * 0.08f - i * 0.4f) * 12);
        gfxRect(fb, x, y, 6, 6, 0, 180 + (i * 3), 80, false);
    }

    gfxRect(fb, 252, 86, 8, 8, 255, 60, 40, false);
    textDraw(fb, "D-PAD RUN  START EXIT", 54, 176, 255, 210, 80, false);
}

void drawLidAnimationOverlay(u8* fb, bool top) {
    if (!lidAnimActive) return;
    int W = top ? 400 : 320;
    int shade = lidAnimFrame < 28 ? lidAnimFrame * 7 : (56 - lidAnimFrame) * 7;
    if (shade < 0) shade = 0;
    if (shade > 190) shade = 190;

    gfxRect(fb, 0, 0, W, shade / 2, 0, 0, 0, top);
    gfxRect(fb, 0, 240 - shade / 2, W, shade / 2, 0, 0, 0, top);
    textDrawCentered(fb, lidAnimFrame < 28 ? "LID CLOSING - AUDIO LOCKED" : "LID OPEN - PANEL RESTORED", 112, 255, 120, 40, top);

    lidAnimFrame++;
    if (lidAnimFrame > 56) {
        lidAnimActive = false;
        lidAnimFrame = 0;
    }
}

void drawIndustrialWarnings(u8* fb, bool top) {
    int W = top ? 400 : 320;

    if (geigerSpikeFrames > 0) {
        for (int i = 0; i < 18; i++) {
            int x = rand() % W;
            int y = rand() % 240;
            gfxRect(fb, x, y, 8 + rand() % 20, 1, 150, 255, 80, top);
        }
        textDrawCentered(fb, "RADIATION PULSE", 204, 120, 255, 80, top);
    }

    if (lowBatterySim && fakeBatteryPct <= 10 && (frameSayaci / 30) % 2 == 0) {
        gfxRect(fb, 20, 20, W - 40, 22, 70, 0, 0, top);
        textDrawCentered(fb, "[SYSTEM POWER LOW]", 28, 255, 60, 40, top);
    }

    if (tapeJamActive && (frameSayaci / 10) % 2 == 0) {
        gfxRect(fb, 46, 198, W - 92, 16, 80, 0, 0, top);
        textDrawCentered(fb, "[TAPE ERROR: SECTOR_0X3F]", 203, 255, 50, 30, top);
    }
}

void browserGoParent(void) {
    if (audioSearchActive) {
        audioSearchRunning = false;
        audioSearchQueueCount = 0;
        loadDirectory(savedBrowserDir);
        fileCursor = 0;
        fileScroll = 0;
        return;
    }

    if (isSdRoot()) {
        uiMode = UI_PLAYER;
        return;
    }

    for (int i = 0; i < browserEntryCount; i++) {
        if (browserEntries[i].isDirectory && strcmp(browserEntries[i].name, "..") == 0) {
            loadDirectory(browserEntries[i].path);
            fileCursor = 0;
            fileScroll = 0;
            return;
        }
    }

    loadDirectory("sdmc:/");
    fileCursor = 0;
    fileScroll = 0;
}

void drawFileBrowser(u8* fb) {
    drawBottomHeader(fb, audioSearchActive ? "AUDIO SEARCH" : "FILE MANAGER");

    gfxRect(fb, 10, 42, 300, 154, 6, 9, 15, false);

    char pathLine[42];
    snprintf(pathLine, sizeof(pathLine), "%.38s", audioSearchActive ? (audioSearchRunning ? "SCANNING SDMC:/ ..." : "SCAN RESULTS FROM SDMC:/") : currentDir);
    textDraw(fb, pathLine, 14, 36, 80, 220, 255, false);

    if (browserEntryCount <= 0) {
        textDrawCentered(fb, "EMPTY DIRECTORY", 100, 255, 100, 60, false);
        char emptyInfo[48];
        snprintf(emptyInfo, sizeof(emptyInfo), "DIR %d FILE %d AUDIO %d", browserDirSeen, browserFileSeen, browserAudioSeen);
        textDraw(fb, emptyInfo, 14, 204, 255, 180, 60, false);
        textDraw(fb, "X SEARCH SD  B BACK/PLAYER", 14, 220, 90, 255, 150, false);
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
            AudioFormat fmt = audioFormatFromFile(browserEntries[idx].path, browserEntries[idx].name);
            const char* tag = "AUD ";
            if (fmt == AUDIO_MP3) tag = "MP3 ";
            else if (fmt == AUDIO_WAV) tag = "WAV ";
            else if (fmt == AUDIO_FLAC) tag = "FLC ";
            else if (fmt == AUDIO_OGG) tag = "OGG ";
            else if (fmt == AUDIO_AAC) tag = "AAC ";
            else if (fmt == AUDIO_M4A) tag = "M4A ";
            else if (fmt == AUDIO_OPUS) tag = "OPS ";
            else tag = "FILE";
            snprintf(displayName, sizeof(displayName), "%s %s", tag, cleanName);
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

    char scanInfo[48];
    snprintf(scanInfo, sizeof(scanInfo), "DIR %d FILE %d AUDIO %d UNS %d", browserDirSeen, browserFileSeen, browserAudioSeen, browserUnsupportedSeen);
    textDraw(fb, scanInfo, 14, 204, 255, 180, 60, false);
    textDraw(fb, audioSearchRunning ? "SCANNING... B CANCEL" : "A OPEN/PLAY  X SEARCH  B BACK", 14, 220, 90, 255, 150, false);
}

void drawSettings(u8* fb) {
    drawBottomHeader(fb, "SETTINGS");

    const char* labels[] = {
        "EQ 60HZ", "EQ 150HZ", "EQ 400HZ", "EQ 1KHZ", "EQ 3KHZ", "EQ 8KHZ", "EQ 14KHZ",
        "VOLUME", "SPEED", "CRT GLOW", "COVER MODE", "SHUFFLE", "REPEAT", "AUTO VIZ",
        "SCANLINE GAP", "SCANLINE STR", "MATRIX LOG", "MOUSE MODE", "GEIGER MODE", "CAM FLARE",
        "CYBER SNAKE", "LOW BATT SIM", "THERMAL", "BATT PCT", "MIC NOISE", "LID ANIM", "TAPE JAM", "PANIC RESET"
    };

    int values[] = {
        eqBand[0], eqBand[1], eqBand[2], eqBand[3], eqBand[4], eqBand[5], eqBand[6],
        masterVolume, playbackRatePct, crtFosforGucu,
        coverMode ? 1 : 0,
        shuffleMode ? 1 : 0,
        repeatMode,
        otomatikDegistir ? 1 : 0,
        scanlineSpacing,
        scanlineStrength,
        matrixLogMode ? 1 : 0,
        mouseMode ? 1 : 0,
        geigerMode ? 1 : 0,
        cameraFlareMode ? 1 : 0,
        cyberSnakeMode ? 1 : 0,
        lowBatterySim ? 1 : 0,
        fakeThermalLevel,
        fakeBatteryPct,
        micNoiseLevel,
        lidAnimActive ? 1 : 0,
        tapeJamActive ? 1 : 0,
        0
    };

    int itemCount = 28;
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
        if ((idx >= 10 && idx <= 11) || idx == 13 || (idx >= 16 && idx <= 21) || idx == 25 || idx == 26) {
            snprintf(val, sizeof(val), "%s", values[idx] ? "ON" : "OFF");
        } else if (idx == 27) {
            snprintf(val, sizeof(val), "EXEC");
        } else {
            snprintf(val, sizeof(val), "%d", values[idx]);
        }

        textDraw(fb, val, 238, y, 0, 255, 150, false);
    }

    textDraw(fb, "LEFT/RIGHT CHANGE  B BACK", 14, 204, 255, 180, 60, false);
    textDraw(fb, "ZL/ZR PRESET  A TOGGLE", 14, 220, 90, 255, 150, false);
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
    if (aktifSesFormati == AUDIO_WAV) textDraw(fb, "WAV", 258, 74, 255, 210, 80, false);
    else if (aktifSesFormati == AUDIO_FLAC) textDraw(fb, "FLC", 258, 74, 255, 210, 80, false);
    else if (aktifSesFormati == AUDIO_OGG) textDraw(fb, "OGG", 258, 74, 255, 210, 80, false);
    else if (aktifSesFormati == AUDIO_MP3) textDraw(fb, "MP3", 258, 74, 0, 220, 255, false);

    char info[42];
    snprintf(info, sizeof(info), "VOL %d  SPD %d  %s", masterVolume, playbackRatePct, caliyor ? "PLAY" : "PAUSE");
    textDraw(fb, info, 18, 94, 130, 220, 255, false);

    float sProgress = sesIlerlemesi01();

    gfxRect(fb, 18, 126, 284, 8, 6, 8, 12, false);

    if (sProgress > 0.0f) {
        gfxRect(fb, 18, 126, (int)(284 * sProgress), 8, 0, 190, 255, false);
    }

    anlikSaniye = sesAnlikSaniye();

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

    textDraw(fb, "A PLAY  X CAM  Y AUTO  B ROT", 18, 152, 255, 190, 70, false);
    textDraw(fb, "L SHUF  R REP  SELECT HOLD", 18, 164, 90, 255, 160, false);

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

    processAudioSearchStep(2);

    if (statusMessageFrames > 0) statusMessageFrames--;
    if (uiBeepCooldown > 0) uiBeepCooldown--;

    if (geigerMode && (!caliyor || viz_anlik_genlik < 5) && (rand() % 140) == 0) {
        geigerSpikeFrames = 12 + rand() % 16;
        triggerUiBeep(15 + rand() % 12);
    }

    if (geigerSpikeFrames > 0) {
        geigerSpikeFrames--;
        if (viz_anlik_genlik < 30) viz_anlik_genlik += 18;
    }

    if (lowBatterySim && fakeBatteryPct <= 10 && playbackRatePct > 95) {
        playbackRatePct = 95;
        sesKanaliniYenidenBaslat();
    }

    u8* fbTopL = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    crtEkranTemizle(fbTopL, true, kameraAcik);

    if (uiMode == UI_STARTUP) {
        drawStartup(fbTopL, true);
    } else if (uiMode == UI_SHUTDOWN) {
        drawShutdown(fbTopL, true);
    } else if (stationStaticFrames > 0) {
        drawStationStatic(fbTopL, true);
    } else if (kameraAcik) {
        kameraFrameCek(fbTopL, true);
    } else {
        vfdProsedurelCizim(fbTopL, slider3D, false);
    }

    drawScanlines(fbTopL, true);
    drawIndustrialWarnings(fbTopL, true);
    drawLidAnimationOverlay(fbTopL, true);

    u8* fbTopR = gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
    crtEkranTemizle(fbTopR, true, kameraAcik);

    if (uiMode == UI_STARTUP) {
        drawStartup(fbTopR, true);
    } else if (uiMode == UI_SHUTDOWN) {
        drawShutdown(fbTopR, true);
    } else if (stationStaticFrames > 0) {
        drawStationStatic(fbTopR, true);
    } else if (kameraAcik) {
        kameraFrameCek(fbTopR, true);
    } else {
        vfdProsedurelCizim(fbTopR, slider3D, true);
    }

    drawScanlines(fbTopR, true);
    drawIndustrialWarnings(fbTopR, true);
    drawLidAnimationOverlay(fbTopR, true);

    u8* fbBot = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);

    if (uiMode == UI_STARTUP) {
        drawStartup(fbBot, false);
    } else if (uiMode == UI_SHUTDOWN) {
        drawShutdown(fbBot, false);
    } else if (uiMode == UI_FILE_BROWSER) {
        drawFileBrowser(fbBot);
    } else if (uiMode == UI_SETTINGS) {
        drawSettings(fbBot);
    } else {
        drawBottomDeck(fbBot);
    }

    drawScanlines(fbBot, false);
    if (uiMode == UI_FILE_BROWSER) drawMatrixLogOverlay(fbBot);
    drawCyberSnakeOverlay(fbBot);
    drawIndustrialWarnings(fbBot, false);
    drawLidAnimationOverlay(fbBot, false);

    if (holdMode) {
        drawHoldOverlay(fbTopL, true);
        drawHoldOverlay(fbTopR, true);
        drawHoldOverlay(fbBot, false);
    }

    if (stationStaticFrames > 0) {
        stationStaticFrames--;
        if (stationStaticFrames == 0) {
            finishPendingTrackChange();
        }
    }

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
    } else if (settingsCursor == 14) {
        scanlineSpacing += delta;
        if (scanlineSpacing < 1) scanlineSpacing = 1;
        if (scanlineSpacing > 8) scanlineSpacing = 8;
    } else if (settingsCursor == 15) {
        scanlineStrength += delta * 4;
        if (scanlineStrength < 0) scanlineStrength = 0;
        if (scanlineStrength > 90) scanlineStrength = 90;
    } else if (settingsCursor == 16) {
        matrixLogMode = !matrixLogMode;
    } else if (settingsCursor == 17) {
        mouseMode = !mouseMode;
    } else if (settingsCursor == 18) {
        geigerMode = !geigerMode;
    } else if (settingsCursor == 19) {
        cameraFlareMode = !cameraFlareMode;
    } else if (settingsCursor == 20) {
        cyberSnakeMode = !cyberSnakeMode;
    } else if (settingsCursor == 21) {
        lowBatterySim = !lowBatterySim;
    } else if (settingsCursor == 22) {
        fakeThermalLevel += delta;
        if (fakeThermalLevel < 25) fakeThermalLevel = 25;
        if (fakeThermalLevel > 85) fakeThermalLevel = 85;
    } else if (settingsCursor == 23) {
        fakeBatteryPct += delta * 5;
        if (fakeBatteryPct < 0) fakeBatteryPct = 0;
        if (fakeBatteryPct > 100) fakeBatteryPct = 100;
    } else if (settingsCursor == 24) {
        micNoiseLevel += delta * 5;
        if (micNoiseLevel < 0) micNoiseLevel = 0;
        if (micNoiseLevel > 100) micNoiseLevel = 100;
    } else if (settingsCursor == 25) {
        triggerLidAnimation();
    } else if (settingsCursor == 26) {
        tapeJamActive = !tapeJamActive;
    } else if (settingsCursor == 27) {
        for (int i = 0; i < 7; i++) eqBand[i] = 100;
        masterVolume = 100;
        playbackRatePct = 100;
        crtFosforGucu = 150;
        scanlineSpacing = 3;
        scanlineStrength = 28;
        setStatusMessage("PANIC RESET COMPLETE");
    }

    triggerUiBeep(3 + settingsCursor);
}
int main(void) {
    srand((unsigned int)time(NULL));

    osSetSpeedupEnable(true);

    gfxInitDefault();
    gfxSet3D(true);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);
    gfxSetDoubleBuffering(GFX_TOP, true);

    ndspInit();
    initBeepEngine();
    romfsInit();
    trigInit();
    aptSetSleepAllowed(false);
    aptHook(&aptCookie, aptEventHook, NULL);

    applyEqPreset(eqPresetIdx);

    strncpy(currentDir, "sdmc:/", sizeof(currentDir));
    loadDirectory(currentDir);

    playingPlaylistCount = 0;
    for (int i = 0; i < browserEntryCount; i++) {
        if (!browserEntries[i].isDirectory && isPlayableAudioFile(browserEntries[i].path, browserEntries[i].name) && playingPlaylistCount < MAKS_SARKI) {
            strncpy(playingPlaylist[playingPlaylistCount].path, browserEntries[i].path, sizeof(playingPlaylist[playingPlaylistCount].path));
            playingPlaylistCount++;
        }
    }

    sesBufferlariniHazirla();

    memset(&buttons, 0, sizeof(buttons));

    startAudioThread();
    rastgeleAnimasyonSec();

    while (aptMainLoop()) {
        hidScanInput();

        u32 kDown = hidKeysDown();
        u32 kUp = hidKeysUp();

        if (kDown & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT | KEY_L | KEY_R | KEY_ZL | KEY_ZR | KEY_SELECT | KEY_START)) {
            triggerUiBeep((int)(kDown & 0xFF));
        }

        if (uiMode == UI_STARTUP) {
            startupFrame++;
            if (startupFrame > 130 || (kDown & KEY_A) || (kDown & KEY_START)) {
                uiMode = UI_FILE_BROWSER;
            }
            renderFrame();
            svcSleepThread(1000000ULL);
            continue;
        }

        if (uiMode == UI_SHUTDOWN) {
            shutdownFrame++;
            renderFrame();
            svcSleepThread(1000000ULL);
            if (shutdownFrame > 55) break;
            continue;
        }

        if (kDown & KEY_SELECT) {
            holdMode = !holdMode;
            memset(&buttons, 0, sizeof(buttons));
        }

        if (holdMode) {
            if (kDown & KEY_START) {
                uiMode = UI_SHUTDOWN;
                shutdownFrame = 0;
            }
            renderFrame();
            svcSleepThread(1000000ULL);
            continue;
        }

        if (kDown & KEY_START) {
            uiMode = UI_SHUTDOWN;
            shutdownFrame = 0;
            continue;
        }

        if (kDown & KEY_A) {
            if (uiMode == UI_FILE_BROWSER) {
                if (browserEntryCount > 0) {
                    int idx = fileCursor;
                    if (browserEntries[idx].isDirectory) {
                        loadDirectory(browserEntries[idx].path);
                        fileCursor = 0;
                        fileScroll = 0;
                    } else {
                        if (!isPlayableAudioFile(browserEntries[idx].path, browserEntries[idx].name)) {
                            setStatusMessage("PLAYABLE: MP3 WAV FLAC OGG");
                            continue;
                        }

                        playingPlaylistCount = 0;
                        int selectedIdxInPlaylist = 0;
                        for (int i = 0; i < browserEntryCount; i++) {
                            if (!browserEntries[i].isDirectory && isPlayableAudioFile(browserEntries[i].path, browserEntries[i].name) && playingPlaylistCount < MAKS_SARKI) {
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
                if (!aktifSesHazir() && playingPlaylistCount > 0) {
                    sarkiYukle(playingPlaylist[aktifSarkiIdx].path);
                }

                caliyor = !caliyor;
                audioThreadPlaying = caliyor;
                cdAnimAktif = true;
                cdAnimFrame = 0;

                if (caliyor) {
                    sesKanaliniYenidenBaslat();
                }
            }
        }

        if (kDown & KEY_B) {
            if (uiMode == UI_SETTINGS) {
                uiMode = settingsReturnMode;
            } else if (uiMode == UI_FILE_BROWSER) {
                browserGoParent();
            } else {
                rotateModFull = !rotateModFull;
            }
        }

        if (kDown & KEY_X) {
            if (uiMode == UI_SETTINGS) {
                coverMode = !coverMode;
            } else if (uiMode == UI_PLAYER) {
                if (!kameraAcik) kameraBaslat();
                else kameraKapat();
            } else if (uiMode == UI_FILE_BROWSER) {
                searchAudioFilesOnSd();
            } else {
                rastgeleAnimasyonSec();
            }
        }

        if (kDown & KEY_Y) {
            if (uiMode == UI_PLAYER) {
                otomatikDegistir = !otomatikDegistir;
            } else if (uiMode == UI_FILE_BROWSER) {
                settingsReturnMode = UI_FILE_BROWSER;
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
            if (browserEntryCount <= 0) {
                fileCursor = 0;
                fileScroll = 0;
            } else {
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
            }
        } else if (uiMode == UI_SETTINGS) {
            if (kDown & KEY_DUP) {
                settingsCursor--;
                if (settingsCursor < 0) settingsCursor = 27;
            }

            if (kDown & KEY_DDOWN) {
                settingsCursor++;
                if (settingsCursor > 27) settingsCursor = 0;
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

            if ((kDown & KEY_DLEFT) && playingPlaylistCount > 0) {
                requestTrackChange(-1);
            }

            if ((kDown & KEY_DRIGHT) && playingPlaylistCount > 0) {
                requestTrackChange(1);
            }
        }

        touchPosition touch;

        if (kDown & KEY_TOUCH) {
            hidTouchRead(&touch);

            int tx = touch.px;
            int ty = touch.py;

            if (tx >= 276 && tx <= 314 && ty >= 4 && ty <= 40) {
                settingsReturnMode = uiMode;
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
                            if (!isPlayableAudioFile(browserEntries[idx].path, browserEntries[idx].name)) {
                                setStatusMessage("PLAYABLE: MP3 WAV FLAC OGG");
                                continue;
                            }

                            playingPlaylistCount = 0;
                            int selectedIdxInPlaylist = 0;
                            for (int i = 0; i < browserEntryCount; i++) {
                                if (!browserEntries[i].isDirectory && isPlayableAudioFile(browserEntries[i].path, browserEntries[i].name) && playingPlaylistCount < MAKS_SARKI) {
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

                if (tx >= 18 && tx <= 302 && ty >= 126 && ty <= 134 && aktifSesHazir()) {
                    float rel = (float)(tx - 18) / 284.0f;

                    if (rel < 0.0f) rel = 0.0f;
                    if (rel > 1.0f) rel = 1.0f;

                    sesYuzdeyeGit(rel);
                }
            }
        }

        if (kUp & KEY_TOUCH && uiMode == UI_PLAYER) {
            if (buttons.play) {
                if (!aktifSesHazir() && playingPlaylistCount > 0) {
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
                    requestTrackChange(-1);
                }

                buttons.prev = false;
            }

            if (buttons.next) {
                if (playingPlaylistCount > 0) {
                    requestTrackChange(1);
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

    aktifSesKapat();
    if (mp3Buffer) free(mp3Buffer);
    if (pcmBufferA) linearFree(pcmBufferA);
    if (pcmBufferB) linearFree(pcmBufferB);
    if (beepPcm) linearFree(beepPcm);

    kameraKapat();

    romfsExit();
    ndspExit();
    aptUnhook(&aptCookie);
    gfxExit();

    return 0;
}
