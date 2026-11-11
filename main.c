/* ============================================================================
 *  STRIKERS 1945  -  C & raylib 클론
 *  ----------------------------------------------------------------------------
 *  개발 환경 : Visual Studio 2022 (C 언어 표준 C17), raylib 6.2
 *  실행 방법 : main.c 하나만 프로젝트에 추가해서 빌드 (추가 에셋 파일 불필요)
 *
 *  조작      : 이동  - 방향키 / WASD
 *              발사  - Z 또는 Space (누르고 있으면 자동 연사)
 *              폭탄  - X   |   일시정지 - P   |   시작/진행 - Enter
 * ==========================================================================*/

#include "raylib.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SCREEN_W 480        /* 화면 가로 (세로형 슈팅게임 비율) */
#define SCREEN_H 640        /* 화면 세로 */

#define MAX_PBULLETS 160    /* 아군 탄환 최대 수 */
#define MAX_EBULLETS 700    /* 적 탄환 최대 수   */
#define MAX_ENEMIES   40
#define MAX_ITEMS     24
#define MAX_PARTS    500

#define PLAYER_SPEED   260.0f
#define PBULLET_SPEED  560.0f

 /* 게임 상태 */
typedef enum { STATE_TITLE, STATE_PLAY, STATE_CLEAR, STATE_OVER } GameState;

/* 탄환 (아군/적 공용) */
typedef struct {
    bool active;
    float x, y, vx, vy, r;
    int dmg;
} Bullet;

/* 적기 : type 0 = 소형 전투기, 1 = 중형 전투기, 2 = 중폭격기 */
typedef struct {
    bool active;
    int type;
    float x, y, vx, vy;
    float t;        /* 움직임 패턴용 타이머 */
    float fireCd;   /* 발사 쿨타임          */
    int hp;
} Enemy;

/* 아이템 : kind 0 = P(무기 강화), 1 = B(폭탄), 2 = GOLD(점수) */
typedef struct {
    bool active;
    int kind;
    float x, y, t;
} Item;

/* 폭발 파티클 */
typedef struct {
    bool active;
    float x, y, vx, vy, size, life, maxLife;
    Color col;
} Particle;

/* 보스 */
typedef struct {
    bool active, dying;
    float x, y, t;
    float fireCd, patternT, dieT, hitT;
    int hp, maxHp, pattern;
} Boss;

/* ------------------------------ 전역 변수 ------------------------------ */
static GameState state = STATE_TITLE;
static bool paused = false;

static Bullet   pb[MAX_PBULLETS];
static Bullet   eb[MAX_EBULLETS];
static Enemy    en[MAX_ENEMIES];
static Item     items[MAX_ITEMS];
static Particle parts[MAX_PARTS];
static Boss     boss;

static float px, py;          /* 플레이어 위치      */
static float fireT;           /* 연사 쿨타임        */
static float invT;            /* 피격 후 무적 시간  */
static float deadT;           /* 사망 연출 타이머   */
static bool  playerDead;
static int   lives, bombs, weaponLv;
static int   score, hiScore, stage;

static float scrollY;         /* 배경 스크롤        */
static float spawnT;          /* 웨이브 스폰 타이머 */
static int   waveNo;
static float bossWarnT;       /* WARNING 연출       */
static float bombFlash;       /* 폭탄 화면 플래시   */

static Sound sShoot, sExpl, sPow, sBomb;

static const Color SEA = { 16,  42,  74, 255 };
static const Color SEA_BAND = { 24,  56,  96, 255 };
static const Color CLOUD = { 220, 226, 232, 110 };

static const float cloudX[5] = { 70.0f, 330.0f, 190.0f, 410.0f, 120.0f };
static const float cloudY[5] = { 0.0f, 150.0f, 300.0f, 450.0f, 580.0f };

/* --------------------------- 간이 효과음 합성기 --------------------------- */
/* 외부 사운드 파일 없이 사각파/노이즈를 직접 합성해서 Sound 로 만든다 */
static Sound MakeSound(float f0, float f1, float dur, float vol, bool noise)
{
    int sr = 22050;
    int n = (int)(sr * dur);
    short* data = (short*)malloc((size_t)n * sizeof(short));
    if (!data) { Sound s = { 0 }; return s; }

    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)sr;
        float env = 1.0f - t / dur;                    /* 감쇠 엔벨로프   */
        float fr = f0 + (f1 - f0) * (t / dur);        /* 피치 슬라이드   */
        float v;
        if (noise) v = (float)(rand() % 2000 - 1000) / 1000.0f;
        else       v = (sinf(2.0f * (float)M_PI * fr * t) >= 0.0f) ? 0.7f : -0.7f;
        data[i] = (short)(v * env * vol * 32767.0f);
    }

    Wave w = { 0 };
    w.frameCount = (unsigned int)n;
    w.sampleRate = (unsigned int)sr;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = data;

    Sound s = LoadSoundFromWave(w);
    free(data);
    return s;
}

/* -------------------------------- 스폰 함수 -------------------------------- */
static void FireEB(float x, float y, float speed, float ang)
{
    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (!eb[i].active) {
            eb[i].active = true;
            eb[i].x = x; eb[i].y = y;
            eb[i].vx = cosf(ang) * speed;
            eb[i].vy = sinf(ang) * speed;
            eb[i].r = 4.0f; eb[i].dmg = 1;
            return;
        }
    }
}

/* 플레이어를 향해 조준 사격 (spread 만큼 각도 편차) */
static void AimAtPlayer(float x, float y, float speed, float spread)
{
    float a = atan2f(py - y, px - x) + spread;
    FireEB(x, y, speed, a);
}

static void SpawnEnemy(int type, float x, float y)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (en[i].active) continue;
        Enemy* e = &en[i];
        e->active = true; e->type = type;
        e->x = x; e->y = y; e->t = 0.0f;
        e->vx = 0.0f; e->vy = 0.0f;
        e->fireCd = 1.0f + (float)GetRandomValue(0, 100) / 100.0f;
        if (type == 0) { e->hp = 1 + stage / 2;  e->vy = 150.0f + stage * 8.0f; }
        else if (type == 1) {
            e->hp = 4 + stage;      e->vy = 95.0f;
            e->vx = (GetRandomValue(0, 1) == 0) ? 70.0f : -70.0f;
        }
        else { e->hp = 14 + stage * 3; e->vy = 60.0f; }
        return;
    }
}

static void SpawnBoss(void)
{
    memset(&boss, 0, sizeof(boss));
    boss.active = true;
    boss.x = SCREEN_W / 2.0f;
    boss.y = -80.0f;
    boss.maxHp = 320 + stage * 130;
    boss.hp = boss.maxHp;
    boss.fireCd = 1.4f;
}

static void SpawnItem(float x, float y, int kind)
{
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!items[i].active) {
            items[i].active = true; items[i].kind = kind;
            items[i].x = x; items[i].y = y; items[i].t = 0.0f;
            return;
        }
    }
}

/* 폭발 파티클 생성 (scale 로 크기 조절) */
static void Explosion(float x, float y, float scale)
{
    int n = (int)(18.0f * scale);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < MAX_PARTS; j++) {
            if (!parts[j].active) {
                float a = (float)GetRandomValue(0, 628) / 100.0f;
                float sp = (float)GetRandomValue(30, 200) * scale;
                parts[j].active = true;
                parts[j].x = x; parts[j].y = y;
                parts[j].vx = cosf(a) * sp;
                parts[j].vy = sinf(a) * sp;
                parts[j].size = (float)GetRandomValue(2, 6) * scale;
                parts[j].maxLife = parts[j].life = 0.3f + (float)GetRandomValue(0, 50) / 100.0f;
                int c = GetRandomValue(0, 2);
                parts[j].col = (c == 0) ? YELLOW : (c == 1) ? ORANGE : RED;
                break;
            }
        }
    }
}

static void KillEnemy(Enemy* e)
{
    static const int scores[3] = { 100, 200, 500 };
    e->active = false;
    Explosion(e->x, e->y, (e->type == 2) ? 1.6f : 1.0f);
    score += scores[e->type];
    PlaySound(sExpl);

    if (e->type == 2) {                     /* 폭격기는 반드시 아이템 드롭 */
        int r = GetRandomValue(0, 9);
        SpawnItem(e->x, e->y, (r < 5) ? 0 : (r < 8) ? 1 : 2);
    }
    else if (e->type == 1 && GetRandomValue(0, 99) < 18) SpawnItem(e->x, e->y, 2);
    else if (e->type == 0 && GetRandomValue(0, 99) < 6) SpawnItem(e->x, e->y, 0);
}

/* ------------------------------ 게임 초기화 ------------------------------ */
static void ClearField(void)
{
    memset(pb, 0, sizeof(pb));
    memset(eb, 0, sizeof(eb));
    memset(en, 0, sizeof(en));
    memset(items, 0, sizeof(items));
    memset(parts, 0, sizeof(parts));
    memset(&boss, 0, sizeof(boss));
}

static void NewStage(void)
{
    ClearField();
    px = SCREEN_W / 2.0f; py = SCREEN_H - 80.0f;
    fireT = 0.0f; invT = 2.0f; deadT = 0.0f;
    playerDead = false;
    spawnT = 1.5f; waveNo = 0; bossWarnT = 0.0f; bombFlash = 0.0f;
}

static void NewGame(void)
{
    lives = 2; bombs = 2; weaponLv = 1;
    score = 0; stage = 1;
    NewStage();
}

/* ------------------------------ 플레이어 ------------------------------ */
static void UpdatePlayer(float dt)
{
    if (playerDead) {                       /* 사망 연출 후 리스폰/게임오버 */
        deadT -= dt;
        if (deadT <= 0.0f) {
            if (lives < 0) state = STATE_OVER;
            else {
                playerDead = false;
                px = SCREEN_W / 2.0f; py = SCREEN_H - 80.0f;
                invT = 2.5f;
            }
        }
        return;
    }

    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) px -= PLAYER_SPEED * dt;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) px += PLAYER_SPEED * dt;
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) py -= PLAYER_SPEED * dt;
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) py += PLAYER_SPEED * dt;

    if (px < 18.0f) px = 18.0f;
    if (px > SCREEN_W - 18.0f) px = SCREEN_W - 18.0f;
    if (py < 40.0f) py = 40.0f;
    if (py > SCREEN_H - 24.0f) py = SCREEN_H - 24.0f;

    if (invT > 0.0f) invT -= dt;
    fireT -= dt;

    /* 자동 연사 : 무기 레벨 1=단발 2=2연장 3=3way 4=5way */
    if ((IsKeyDown(KEY_Z) || IsKeyDown(KEY_SPACE)) && fireT <= 0.0f) {
        fireT = 0.13f;
        float ang[5];
        int cnt = 0;
        switch (weaponLv) {
        case 1:  ang[0] = 0.0f; cnt = 1; break;
        case 2:  ang[0] = 0.0f; cnt = 1; break;
        case 3:  ang[0] = -0.10f; ang[1] = 0.0f; ang[2] = 0.10f; cnt = 3; break;
        default: ang[0] = -0.20f; ang[1] = -0.10f; ang[2] = 0.0f;
            ang[3] = 0.10f; ang[4] = 0.20f; cnt = 5; break;
        }
        float offs[2] = { -7.0f, 7.0f };
        int nShot = (weaponLv == 2) ? 2 : cnt;
        for (int s = 0; s < nShot; s++) {
            for (int i = 0; i < MAX_PBULLETS; i++) {
                if (!pb[i].active) {
                    float a = (weaponLv == 2) ? 0.0f : ang[s];
                    pb[i].active = true;
                    pb[i].x = px + ((weaponLv == 2) ? offs[s] : 0.0f);
                    pb[i].y = py - 14.0f;
                    pb[i].vx = sinf(a) * PBULLET_SPEED;
                    pb[i].vy = -cosf(a) * PBULLET_SPEED;
                    pb[i].r = 3.0f; pb[i].dmg = 1;
                    break;
                }
            }
        }
        PlaySound(sShoot);
    }

    /* 폭탄 : 화면의 적 탄환 전부 소멸 + 적에게 큰 피해 */
    if (IsKeyPressed(KEY_X) && bombs > 0 && bombFlash <= 0.0f) {
        bombs--;
        bombFlash = 0.7f;
        PlaySound(sBomb);
        for (int i = 0; i < MAX_EBULLETS; i++) {
            if (eb[i].active) { Explosion(eb[i].x, eb[i].y, 0.15f); eb[i].active = false; }
        }
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (en[i].active) {
                en[i].hp -= 20;
                if (en[i].hp <= 0) KillEnemy(&en[i]);
            }
        }
        if (boss.active && !boss.dying) { boss.hp -= 40; boss.hitT = 0.1f; }
    }
}

/* ------------------------------ 적 웨이브 ------------------------------ */
static void UpdateWaves(float dt)
{
    if (boss.active) return;

    if (bossWarnT > 0.0f) {                 /* WARNING 표시 후 보스 등장 */
        bossWarnT -= dt;
        if (bossWarnT <= 0.0f) SpawnBoss();
        return;
    }

    spawnT -= dt;
    if (spawnT > 0.0f) return;

    float diff = 1.0f - (float)(stage - 1) * 0.07f;     /* 스테이지가 오를수록 빨라짐 */
    if (diff < 0.6f) diff = 0.6f;

    switch (waveNo % 4) {
    case 0: {   /* 소형 전투기 쐐기 편대 */
        float cx = 100.0f + (float)GetRandomValue(0, 280);
        for (int i = 0; i < 5; i++)
            SpawnEnemy(0, cx + (float)(i - 2) * 45.0f, -30.0f - (float)abs(i - 2) * 35.0f);
        spawnT = 3.1f * diff;
    } break;
    case 1:     /* 중형기 2대 (좌우 왕복 사수) */
        SpawnEnemy(1, 80.0f, -40.0f);
        SpawnEnemy(1, SCREEN_W - 80.0f, -40.0f);
        spawnT = 2.6f * diff;
        break;
    case 2:     /* 소형기 좌우 교차 */
        for (int i = 0; i < 4; i++) {
            SpawnEnemy(0, 40.0f + i * 30.0f, -30.0f - i * 60.0f);
            SpawnEnemy(0, SCREEN_W - 40.0f - i * 30.0f, -30.0f - i * 60.0f);
        }
        spawnT = 3.5f * diff;
        break;
    default:    /* 중폭격기 + 호위기 */
        SpawnEnemy(2, SCREEN_W / 2.0f, -50.0f);
        SpawnEnemy(0, 110.0f, -30.0f);
        SpawnEnemy(0, SCREEN_W - 110.0f, -30.0f);
        spawnT = 4.0f * diff;
        break;
    }
    waveNo++;

    if (waveNo >= 5 + stage * 2) bossWarnT = 2.5f;      /* 일정 웨이브 후 보스 */
}

/* ------------------------------ 적 업데이트 ------------------------------ */
static float EBulletSpeed(void)
{
    float s = 130.0f + (float)stage * 10.0f;
    return (s > 220.0f) ? 220.0f : s;
}

static void UpdateEnemies(float dt)
{
    float bs = EBulletSpeed();
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy* e = &en[i];
        if (!e->active) continue;
        e->t += dt;
        e->fireCd -= dt;

        if (e->type == 0) {                     /* 소형 : 직진 + 미세 흔들림, 가끔 조준탄 */
            e->y += e->vy * dt;
            e->x += sinf(e->t * 3.0f) * 25.0f * dt;
            if (e->fireCd <= 0.0f) {
                e->fireCd = 2.0f;
                if (e->y > 0.0f && e->y < py - 40.0f && GetRandomValue(0, 99) < 35)
                    AimAtPlayer(e->x, e->y, bs, 0.0f);
            }
        }
        else if (e->type == 1) {                /* 중형 : 좌우 왕복하며 조준 사격 */
            e->y += e->vy * dt * 0.4f;
            e->x += e->vx * dt;
            if (e->x < 40.0f) { e->x = 40.0f;              e->vx = -e->vx; }
            if (e->x > SCREEN_W - 40.0f) { e->x = SCREEN_W - 40.0f;   e->vx = -e->vx; }
            if (e->fireCd <= 0.0f) {
                e->fireCd = 1.7f;
                if (e->y > 0.0f) AimAtPlayer(e->x, e->y, bs, 0.0f);
            }
        }
        else {                                  /* 폭격기 : 상공 호버링 + 3연발 부채탄 */
            if (e->y < 110.0f) e->y += e->vy * dt;
            else e->x = SCREEN_W / 2.0f + sinf(e->t * 0.7f) * 120.0f;
            if (e->fireCd <= 0.0f) {
                e->fireCd = 2.3f;
                if (e->y > 0.0f) {
                    AimAtPlayer(e->x, e->y + 10.0f, bs, -0.18f);
                    AimAtPlayer(e->x, e->y + 10.0f, bs, 0.0f);
                    AimAtPlayer(e->x, e->y + 10.0f, bs, 0.18f);
                }
            }
        }

        if (e->y > SCREEN_H + 60.0f || e->x < -80.0f || e->x > SCREEN_W + 80.0f)
            e->active = false;
    }
}

/* ------------------------------ 보스 ------------------------------ */
static void UpdateBoss(float dt)
{
    if (!boss.active) return;

    if (!boss.dying && boss.hp <= 0) {      /* 격추 시작 : 연쇄 폭발 연출 */
        boss.dying = true;
        boss.dieT = 1.6f;
    }

    if (boss.dying) {
        boss.dieT -= dt;
        if (GetRandomValue(0, 99) < 40)
            Explosion(boss.x + (float)GetRandomValue(-60, 60),
                boss.y + (float)GetRandomValue(-20, 20), 0.8f);
        if (boss.dieT <= 0.0f) {
            boss.active = false;
            Explosion(boss.x, boss.y, 3.0f);
            score += 5000 * stage;
            PlaySound(sBomb);
            state = STATE_CLEAR;
        }
        return;
    }

    boss.t += dt;
    if (boss.hitT > 0.0f) boss.hitT -= dt;

    if (boss.y < 100.0f) boss.y += 70.0f * dt;                      /* 등장 */
    else boss.x = SCREEN_W / 2.0f + sinf(boss.t * 0.8f) * 130.0f;   /* 좌우 왕복 */

    if (boss.y < 50.0f) return;                                     /* 등장 전 사격 금지 */

    boss.patternT += dt;
    if (boss.patternT > 3.5f) {                                     /* 3.5초마다 패턴 교체 */
        boss.patternT = 0.0f;
        boss.pattern = (boss.pattern + 1) % 3;
    }

    boss.fireCd -= dt;
    if (boss.fireCd > 0.0f) return;

    float bs = EBulletSpeed() + 20.0f;
    if (boss.pattern == 0) {                        /* 패턴 1 : 양쪽 엔진 조준탄 연사 */
        boss.fireCd = 0.45f;
        AimAtPlayer(boss.x - 30.0f, boss.y + 20.0f, bs, 0.0f);
        AimAtPlayer(boss.x + 30.0f, boss.y + 20.0f, bs, 0.0f);
    }
    else if (boss.pattern == 1) {                   /* 패턴 2 : 원형 탄막 */
        boss.fireCd = 1.3f;
        int n = 16 + stage * 2;
        float base = (float)GetRandomValue(0, 628) / 100.0f;
        for (int i = 0; i < n; i++)
            FireEB(boss.x, boss.y + 10.0f, bs * 0.8f, base + (float)i * (2.0f * (float)M_PI / (float)n));
    }
    else {                                          /* 패턴 3 : 부채꼴 5연발 */
        boss.fireCd = 1.4f;
        for (int k = -2; k <= 2; k++)
            AimAtPlayer(boss.x, boss.y + 20.0f, bs, (float)k * 0.16f);
    }
}

/* ------------------------------ 탄환/아이템/파티클 ------------------------------ */
static void UpdateBullets(float dt)
{
    for (int i = 0; i < MAX_PBULLETS; i++) {
        if (!pb[i].active) continue;
        pb[i].x += pb[i].vx * dt;
        pb[i].y += pb[i].vy * dt;
        if (pb[i].y < -20.0f || pb[i].x < -20.0f || pb[i].x > SCREEN_W + 20.0f)
            pb[i].active = false;
    }
    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (!eb[i].active) continue;
        eb[i].x += eb[i].vx * dt;
        eb[i].y += eb[i].vy * dt;
        if (eb[i].y < -20.0f || eb[i].y > SCREEN_H + 20.0f ||
            eb[i].x < -20.0f || eb[i].x > SCREEN_W + 20.0f)
            eb[i].active = false;
    }
}

static void UpdateItems(float dt)
{
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!items[i].active) continue;
        items[i].t += dt;
        items[i].y += 55.0f * dt;
        items[i].x += sinf(items[i].t * 2.0f) * 18.0f * dt;
        if (items[i].y > SCREEN_H + 20.0f) items[i].active = false;
    }
}

static void UpdateParticles(float dt)
{
    for (int i = 0; i < MAX_PARTS; i++) {
        if (!parts[i].active) continue;
        parts[i].life -= dt;
        if (parts[i].life <= 0.0f) { parts[i].active = false; continue; }
        parts[i].x += parts[i].vx * dt;
        parts[i].y += parts[i].vy * dt;
        parts[i].vx *= (1.0f - 1.5f * dt);
        parts[i].vy *= (1.0f - 1.5f * dt);
    }
}

/* ------------------------------ 충돌 처리 ------------------------------ */
static float EnemyRadius(const Enemy* e)
{
    return (e->type == 0) ? 13.0f : (e->type == 1) ? 17.0f : 26.0f;
}

static void HitPlayer(void)
{
    Explosion(px, py, 2.0f);
    PlaySound(sExpl);
    lives--;
    if (weaponLv > 1) weaponLv--;           /* 피격 시 무기 1단계 강등 */
    playerDead = true;
    deadT = 1.2f;
}

static void UpdateCollisions(void)
{
    /* 아군 탄환 -> 적기 / 보스 */
    for (int i = 0; i < MAX_PBULLETS; i++) {
        if (!pb[i].active) continue;

        for (int j = 0; j < MAX_ENEMIES; j++) {
            if (!en[j].active) continue;
            float rr = pb[i].r + EnemyRadius(&en[j]);
            float dx = pb[i].x - en[j].x, dy = pb[i].y - en[j].y;
            if (dx * dx + dy * dy < rr * rr) {
                pb[i].active = false;
                en[j].hp -= pb[i].dmg;
                if (en[j].hp <= 0) KillEnemy(&en[j]);
                break;
            }
        }

        if (pb[i].active && boss.active && !boss.dying) {
            float rr = pb[i].r + 44.0f;
            float dx = pb[i].x - boss.x, dy = pb[i].y - boss.y;
            if (dx * dx + dy * dy < rr * rr) {
                pb[i].active = false;
                boss.hp -= pb[i].dmg;
                boss.hitT = 0.06f;
                score += 10;
            }
        }
    }

    /* 아이템 획득 */
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!items[i].active) continue;
        float dx = items[i].x - px, dy = items[i].y - py;
        if (dx * dx + dy * dy < 22.0f * 22.0f) {
            items[i].active = false;
            PlaySound(sPow);
            if (items[i].kind == 0) { if (weaponLv < 4) weaponLv++; score += 100; }
            else if (items[i].kind == 1) { if (bombs < 3) bombs++;       score += 100; }
            else                         score += 1000;
        }
    }

    if (playerDead || invT > 0.0f) return;  /* 무적/사망 중 피격 무시 */

    /* 적 탄환 -> 플레이어 (실제 슈팅게임처럼 작은 판정) */
    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (!eb[i].active) continue;
        float rr = eb[i].r + 6.0f;
        float dx = eb[i].x - px, dy = eb[i].y - py;
        if (dx * dx + dy * dy < rr * rr) {
            eb[i].active = false;
            HitPlayer();
            return;
        }
    }

    /* 적기 몸통 -> 플레이어 */
    for (int j = 0; j < MAX_ENEMIES; j++) {
        if (!en[j].active) continue;
        float rr = EnemyRadius(&en[j]) + 8.0f;
        float dx = en[j].x - px, dy = en[j].y - py;
        if (dx * dx + dy * dy < rr * rr) {
            KillEnemy(&en[j]);
            HitPlayer();
            return;
        }
    }
}

/* ------------------------------ 전체 업데이트 ------------------------------ */
static void Update(float dt)
{
    if (dt > 0.05f) dt = 0.05f;             /* 프레임 드랍 시 튐 방지 */
    scrollY += 40.0f * dt;
    if (bombFlash > 0.0f) bombFlash -= dt;

    if (state == STATE_TITLE) {
        if (IsKeyPressed(KEY_ENTER)) { NewGame(); state = STATE_PLAY; paused = false; }
        return;
    }

    if (state == STATE_CLEAR || state == STATE_OVER) {
        UpdateParticles(dt);
        if (IsKeyPressed(KEY_ENTER)) {
            if (state == STATE_CLEAR) { stage++; NewStage(); state = STATE_PLAY; }
            else                      state = STATE_TITLE;
        }
        return;
    }

    if (IsKeyPressed(KEY_P)) paused = !paused;
    if (paused) return;

    UpdatePlayer(dt);
    UpdateWaves(dt);
    UpdateEnemies(dt);
    UpdateBoss(dt);
    UpdateBullets(dt);
    UpdateItems(dt);
    UpdateParticles(dt);
    UpdateCollisions();
    if (score > hiScore) hiScore = score;
}

/* ------------------------------ 그리기 ------------------------------ */
static void DrawBackground(void)
{
    ClearBackground(SEA);
    for (int i = -1; i < 9; i++) {          /* 흘러가는 바다 무늬 */
        float y = fmodf((float)i * 90.0f + scrollY, (float)(SCREEN_H + 90)) - 90.0f;
        DrawRectangle(0, (int)y, SCREEN_W, 26, SEA_BAND);
    }
    for (int i = 0; i < 5; i++) {           /* 구름 */
        float y = fmodf(cloudY[i] + scrollY * 1.5f, (float)(SCREEN_H + 120)) - 60.0f;
        DrawEllipse((int)cloudX[i], (int)y, 48.0f, 16.0f, CLOUD);
        DrawEllipse((int)cloudX[i] + 26, (int)y - 8, 30.0f, 12.0f, CLOUD);
    }
}

static void DrawPlayer(void)
{
    if (playerDead) return;
    if (invT > 0.0f && ((int)(invT * 12.0f) % 2 == 0)) return;  /* 무적 중 깜빡임 */

    /* 주익 / 동체 / 꼬리날개 */
    DrawTriangle((Vector2) { px - 18.0f, py + 5.0f },
        (Vector2) {
        px + 18.0f, py + 5.0f
    },
        (Vector2) {
        px, py + 10.0f
    }, GRAY);
    DrawTriangle((Vector2) { px, py - 16.0f },
        (Vector2) {
        px - 6.0f, py + 10.0f
    },
        (Vector2) {
        px + 6.0f, py + 10.0f
    }, LIGHTGRAY);
    DrawTriangle((Vector2) { px - 8.0f, py + 12.0f },
        (Vector2) {
        px + 8.0f, py + 12.0f
    },
        (Vector2) {
        px, py + 16.0f
    }, DARKGRAY);
    DrawCircleV((Vector2) { px, py - 4.0f }, 3.0f, SKYBLUE);     /* 조종석 */
    DrawCircleV((Vector2) { px, py - 14.0f }, 2.5f, RED);        /* 기수 */

    float fl = (float)GetRandomValue(6, 12);                    /* 엔진 화염 */
    DrawTriangle((Vector2) { px - 3.0f, py + 13.0f },
        (Vector2) {
        px + 3.0f, py + 13.0f
    },
        (Vector2) {
        px, py + 13.0f + fl
    }, ORANGE);
}

static void DrawEnemies(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!en[i].active) continue;
        float x = en[i].x, y = en[i].y;

        if (en[i].type == 0) {              /* 소형 전투기 (붉은색) */
            DrawTriangle((Vector2) { x - 14.0f, y - 2.0f },
                (Vector2) {
                x + 14.0f, y - 2.0f
            },
                (Vector2) {
                x, y - 7.0f
            }, RED);
            DrawTriangle((Vector2) { x, y + 14.0f },
                (Vector2) {
                x - 6.0f, y - 8.0f
            },
                (Vector2) {
                x + 6.0f, y - 8.0f
            }, MAROON);
            DrawCircleV((Vector2) { x, y + 5.0f }, 2.5f, YELLOW);
        }
        else if (en[i].type == 1) {         /* 중형 전투기 (녹색) */
            DrawTriangle((Vector2) { x - 19.0f, y - 3.0f },
                (Vector2) {
                x + 19.0f, y - 3.0f
            },
                (Vector2) {
                x, y - 9.0f
            }, GREEN);
            DrawTriangle((Vector2) { x, y + 17.0f },
                (Vector2) {
                x - 8.0f, y - 10.0f
            },
                (Vector2) {
                x + 8.0f, y - 10.0f
            }, DARKGREEN);
            DrawCircleV((Vector2) { x - 7.0f, y }, 3.0f, LIME);
            DrawCircleV((Vector2) { x + 7.0f, y }, 3.0f, LIME);
        }
        else {                              /* 중폭격기 (갈색) */
            DrawRectangle((int)x - 34, (int)y - 6, 68, 14, DARKBROWN);
            DrawTriangle((Vector2) { x, y + 24.0f },
                (Vector2) {
                x - 9.0f, y - 14.0f
            },
                (Vector2) {
                x + 9.0f, y - 14.0f
            }, BROWN);
            DrawCircleV((Vector2) { x - 20.0f, y + 1.0f }, 5.0f, DARKGRAY);
            DrawCircleV((Vector2) { x + 20.0f, y + 1.0f }, 5.0f, DARKGRAY);
            DrawCircleV((Vector2) { x, y + 10.0f }, 3.0f, YELLOW);
        }
    }
}

static void DrawBoss(void)
{
    if (!boss.active) return;
    float x = boss.x, y = boss.y;

    DrawRectangle((int)x - 70, (int)y - 10, 140, 24, DARKGRAY);     /* 넓은 주익 */
    DrawTriangle((Vector2) { x, y + 40.0f },
        (Vector2) {
        x - 16.0f, y - 26.0f
    },
        (Vector2) {
        x + 16.0f, y - 26.0f
    }, GRAY);
    DrawCircleV((Vector2) { x - 40.0f, y + 2.0f }, 9.0f, BLACK);     /* 엔진 */
    DrawCircleV((Vector2) { x + 40.0f, y + 2.0f }, 9.0f, BLACK);
    DrawCircleV((Vector2) { x - 40.0f, y + 2.0f }, 5.0f, ORANGE);
    DrawCircleV((Vector2) { x + 40.0f, y + 2.0f }, 5.0f, ORANGE);
    DrawCircleV((Vector2) { x, y - 6.0f }, 6.0f, SKYBLUE);           /* 조종석 */

    if (boss.hitT > 0.0f) {                                         /* 피격 플래시 */
        DrawRectangle((int)x - 70, (int)y - 10, 140, 24, Fade(WHITE, 0.7f));
        DrawCircleV((Vector2) { x, y }, 26.0f, Fade(WHITE, 0.5f));
    }

    int bw = (int)((SCREEN_W - 120) * ((float)boss.hp / (float)boss.maxHp));
    if (bw < 0) bw = 0;
    DrawRectangle(60, 34, SCREEN_W - 120, 8, Fade(BLACK, 0.5f));    /* 체력바 */
    DrawRectangle(60, 34, bw, 8, RED);
    DrawRectangleLines(60, 34, SCREEN_W - 120, 8, WHITE);
}

static void DrawBullets(void)
{
    for (int i = 0; i < MAX_PBULLETS; i++)
        if (pb[i].active)
            DrawRectangle((int)pb[i].x - 1, (int)pb[i].y - 6, 3, 12, YELLOW);

    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (!eb[i].active) continue;
        DrawCircleV((Vector2) { eb[i].x, eb[i].y }, eb[i].r, MAGENTA);
        DrawCircleV((Vector2) { eb[i].x, eb[i].y }, eb[i].r * 0.5f, WHITE);
    }
}

static void DrawItems(void)
{
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!items[i].active) continue;
        Vector2 c = { items[i].x, items[i].y };
        if (items[i].kind == 0) { DrawCircleV(c, 9.0f, SKYBLUE); DrawText("P", (int)c.x - 5, (int)c.y - 8, 16, WHITE); }
        else if (items[i].kind == 1) { DrawCircleV(c, 9.0f, MAROON);  DrawText("B", (int)c.x - 5, (int)c.y - 8, 16, WHITE); }
        else { DrawCircleV(c, 9.0f, GOLD);    DrawText("$", (int)c.x - 5, (int)c.y - 8, 16, WHITE); }
    }
}

static void DrawParticles(void)
{
    for (int i = 0; i < MAX_PARTS; i++) {
        if (!parts[i].active) continue;
        float a = parts[i].life / parts[i].maxLife;
        DrawCircleV((Vector2) { parts[i].x, parts[i].y }, parts[i].size* a, Fade(parts[i].col, a));
    }
}

static void DrawHUD(void)
{
    DrawText(TextFormat("SCORE %07d", score), 10, 8, 20, WHITE);
    const char* hi = TextFormat("HI %07d", hiScore);
    DrawText(hi, SCREEN_W - MeasureText(hi, 20) - 10, 8, 20, YELLOW);

    const char* st = TextFormat("STAGE %d", stage);
    DrawText(st, SCREEN_W / 2 - MeasureText(st, 20) / 2, 8, 20, WHITE);

    for (int i = 0; i < lives; i++) {           /* 잔기 표시 */
        float bx = 20.0f + i * 22.0f, by = SCREEN_H - 20.0f;
        DrawTriangle((Vector2) { bx, by - 7.0f },
            (Vector2) {
            bx - 6.0f, by + 6.0f
        },
            (Vector2) {
            bx + 6.0f, by + 6.0f
        }, LIGHTGRAY);
    }
    DrawText(TextFormat("POWER %d", weaponLv), 70, SCREEN_H - 27, 16, SKYBLUE);

    for (int i = 0; i < bombs; i++)             /* 폭탄 표시 */
        DrawCircle(SCREEN_W - 20 - i * 20, SCREEN_H - 20, 7.0f, RED);
}

static void DrawTitle(void)
{
    const char* t1 = "STRIKERS 1945";
    DrawText(t1, SCREEN_W / 2 - MeasureText(t1, 40) / 2, 140, 40, YELLOW);
    const char* t2 = "C / RAYLIB EDITION";
    DrawText(t2, SCREEN_W / 2 - MeasureText(t2, 20) / 2, 192, 20, SKYBLUE);

    DrawText("MOVE : ARROW / WASD", 125, 300, 20, WHITE);
    DrawText("SHOT : Z / SPACE (AUTO)", 125, 332, 20, WHITE);
    DrawText("BOMB : X", 125, 364, 20, WHITE);

    if ((int)(GetTime() * 2.0) % 2 == 0) {
        const char* p = "PRESS ENTER TO START";
        DrawText(p, SCREEN_W / 2 - MeasureText(p, 20) / 2, 460, 20, LIME);
    }
}

static void DrawClear(void)
{
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.55f));
    const char* t = "STAGE CLEAR!";
    DrawText(t, SCREEN_W / 2 - MeasureText(t, 36) / 2, 220, 36, GOLD);
    const char* s = TextFormat("SCORE %07d", score);
    DrawText(s, SCREEN_W / 2 - MeasureText(s, 20) / 2, 290, 20, WHITE);
    if ((int)(GetTime() * 2.0) % 2 == 0) {
        const char* p = "PRESS ENTER FOR NEXT STAGE";
        DrawText(p, SCREEN_W / 2 - MeasureText(p, 18) / 2, 380, 18, LIME);
    }
}

static void DrawOver(void)
{
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.55f));
    const char* t = "GAME OVER";
    DrawText(t, SCREEN_W / 2 - MeasureText(t, 40) / 2, 220, 40, RED);
    const char* s = TextFormat("FINAL SCORE %07d", score);
    DrawText(s, SCREEN_W / 2 - MeasureText(s, 20) / 2, 300, 20, WHITE);
    if ((int)(GetTime() * 2.0) % 2 == 0) {
        const char* p = "PRESS ENTER";
        DrawText(p, SCREEN_W / 2 - MeasureText(p, 18) / 2, 380, 18, LIME);
    }
}

static void Draw(void)
{
    BeginDrawing();
    DrawBackground();

    if (state == STATE_TITLE) {
        DrawTitle();
    }
    else {
        DrawItems();
        DrawEnemies();
        DrawBoss();
        DrawBullets();
        DrawPlayer();
        DrawParticles();
        DrawHUD();

        if (bossWarnT > 0.0f && state == STATE_PLAY) {      /* 보스 경고 */
            if ((int)(GetTime() * 6.0) % 2 == 0) {
                const char* w = "- WARNING -";
                DrawText(w, SCREEN_W / 2 - MeasureText(w, 30) / 2, 200, 30, RED);
            }
        }

        if (paused) {
            const char* p = "PAUSED";
            DrawText(p, SCREEN_W / 2 - MeasureText(p, 30) / 2, 300, 30, WHITE);
        }

        if (state == STATE_CLEAR) DrawClear();
        if (state == STATE_OVER)  DrawOver();
    }

    if (bombFlash > 0.0f)                                   /* 폭탄 플래시 */
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(WHITE, bombFlash * 0.8f));

    EndDrawing();
}

/* ------------------------------ 메인 ------------------------------ */
int main(void)
{
    srand((unsigned int)time(NULL));

    InitWindow(SCREEN_W, SCREEN_H, "STRIKERS 1945 - C / raylib");
    SetTargetFPS(60);
    InitAudioDevice();

    sShoot = MakeSound(900.0f, 500.0f, 0.06f, 0.15f, false);   /* 발사   */
    sExpl = MakeSound(0.0f, 0.0f, 0.35f, 0.50f, true);    /* 폭발   */
    sPow = MakeSound(500.0f, 1200.0f, 0.20f, 0.35f, false);   /* 파워업 */
    sBomb = MakeSound(0.0f, 0.0f, 0.70f, 0.60f, true);    /* 폭탄   */

    while (!WindowShouldClose()) {
        Update(GetFrameTime());
        Draw();
    }

    UnloadSound(sShoot);
    UnloadSound(sExpl);
    UnloadSound(sPow);
    UnloadSound(sBomb);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
