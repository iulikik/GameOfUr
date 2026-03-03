// =============================================================================
//  Royal Game of Ur  —  LilyGo T-Display-S3  (graphical, Finkel rules)
//
//  Controls
//    PIN_BUTTON_NAV  (GPIO 0 )  — cycle through options / pieces
//    PIN_BUTTON_ACT  (GPIO 14)  — confirm / roll dice
//
//  Board layout (3 rows × 8 cols, 35 px per cell)
//
//   Row 0:  [P2 entry: cols 0-3]  [GAP: cols 4-5]  [P2 exit: cols 6-7]
//   Row 1:  [        shared track: cols 0-7        ]
//   Row 2:  [P1 entry: cols 0-3]  [GAP: cols 4-5]  [P1 exit: cols 6-7]
//
//  The GAP cells display home-piece and finished-piece counts.
//  Rosettes sit at board cells {0,0}, {0,6}, {1,3}, {2,0}, {2,6}.
//
//  Each player's 14-step path (positions 0-13):
//   P1  {2,3}→…→{2,0} | {1,0}→…→{1,7} | {2,7}→{2,6}
//   P2  {0,3}→…→{0,0} | {1,0}→…→{1,7} | {0,7}→{0,6}
//  Rosette path positions: 3, 7, 13.
//  Position -1       = home (not yet entered)
//  Position PATH_LEN = borne off (finished)
// =============================================================================

#include <Arduino.h>
#include <TFT_eSPI.h>

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------
#define PIN_BUTTON_NAV  0
#define PIN_BUTTON_ACT  14

// ---------------------------------------------------------------------------
// Game constants
// ---------------------------------------------------------------------------
static const int NUM_PIECES = 7;
static const int PATH_LEN   = 14;   // on-board positions: 0..13
                                    // -1 = home,  PATH_LEN = finished

// ---------------------------------------------------------------------------
// Visual layout
// ---------------------------------------------------------------------------
static const int SQ = 35;   // square size (pixels)
static const int BX = 10;   // board left edge
static const int BY = 28;   // board top edge
// Screen 320×170 (rotation 3).
// Board: x 10-290, y 28-133.  Message bar: y 0-27.  Hint bar: y 133-170.

// ---------------------------------------------------------------------------
// Path coordinate tables
// ---------------------------------------------------------------------------
struct Coord { uint8_t row; uint8_t col; };

// Player 0 — Blue, bottom lanes
static const Coord P1_PATH[14] = {
    {2,3},{2,2},{2,1},{2,0},    // private entry pos 0-3  ([2][0] = rosette)
    {1,0},{1,1},{1,2},{1,3},    // shared        pos 4-7  ([1][3] = rosette)
    {1,4},{1,5},{1,6},{1,7},    // shared        pos 8-11
    {2,7},{2,6}                 // private exit  pos 12-13 ([2][6] = rosette)
};

// Player 1 — Red, top lanes
static const Coord P2_PATH[14] = {
    {0,3},{0,2},{0,1},{0,0},    // private entry pos 0-3  ([0][0] = rosette)
    {1,0},{1,1},{1,2},{1,3},    // shared        pos 4-7
    {1,4},{1,5},{1,6},{1,7},    // shared        pos 8-11
    {0,7},{0,6}                 // private exit  pos 12-13 ([0][6] = rosette)
};

inline const Coord& pathCoord(int p, int pos) {
    return (p == 0) ? P1_PATH[pos] : P2_PATH[pos];
}

// Board cells that carry a rosette ornament
static const Coord ROSETTE_CELLS[5] = {{0,0},{0,6},{1,3},{2,0},{2,6}};

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------
enum GameState { GS_MODE_SELECT, GS_ROLL, GS_SELECT_PIECE, GS_AI_MOVE, GS_GAME_OVER };
enum GameMode  { GM_PVP, GM_PvAI };

struct Player {
    const char* name;
    uint16_t    color;
    int         pos[NUM_PIECES];  // -1 / 0..13 / PATH_LEN
    int         finished;
    bool        isAI;
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
TFT_eSPI    tft    = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

GameState gState;
GameMode  gMode;
Player    players[2];
int       curPlayer;
bool      extraTurn;
int       curRoll;
int       diceVals[4];
bool      diceRolled;

int       legalMoves[NUM_PIECES];
int       numLegal;
int       navIdx;    // cursor within legalMoves[]
int       menuSel;   // 0 = PvP, 1 = PvAI

bool          aiPending  = false;
unsigned long aiTimer    = 0;
static const unsigned int AI_DELAY_MS = 1400;

// ============================================================================
//  GAME LOGIC
// ============================================================================

bool isRosette(int p)  { return p == 3 || p == 7 || p == 13; }
bool isShared (int p)  { return p >= 4 && p <= 11; }

int rollDice() {
    int total = 0;
    for (int i = 0; i < 4; i++) { diceVals[i] = random(0, 2); total += diceVals[i]; }
    return total;
}

// Destination path-position for player p, piece idx, roll r.  -1 = illegal.
int getTarget(int p, int idx, int r) {
    int cur = players[p].pos[idx];
    if (cur == PATH_LEN) return -1;

    int dest;
    if (cur == -1) {
        if (r == 0) return -1;
        dest = r - 1;           // roll 1 → pos 0,  roll 4 → pos 3
    } else {
        dest = cur + r;
    }
    if (dest > PATH_LEN) return -1;
    if (dest == PATH_LEN) return PATH_LEN;  // exact bear-off

    // Own piece already on dest?
    for (int i = 0; i < NUM_PIECES; i++)
        if (i != idx && players[p].pos[i] == dest) return -1;

    // Opponent piece safe on a rosette in the shared zone?
    if (isShared(dest) && isRosette(dest)) {
        int opp = 1 - p;
        for (int i = 0; i < NUM_PIECES; i++)
            if (players[opp].pos[i] == dest) return -1;
    }

    return dest;
}

void computeLegal(int p, int r) {
    numLegal = 0;
    if (r == 0) return;
    for (int i = 0; i < NUM_PIECES; i++)
        if (getTarget(p, i, r) != -1)
            legalMoves[numLegal++] = i;
}

void doMove(int p, int idx) {
    int dest = getTarget(p, idx, curRoll);

    // Capture: opponent in shared zone on dest
    if (dest >= 0 && dest < PATH_LEN && isShared(dest)) {
        int opp = 1 - p;
        for (int i = 0; i < NUM_PIECES; i++)
            if (players[opp].pos[i] == dest) players[opp].pos[i] = -1;
    }

    players[p].pos[idx] = dest;
    if (dest == PATH_LEN) players[p].finished++;
    extraTurn = (dest < PATH_LEN && isRosette(dest));
}

bool checkWin(int p) { return players[p].finished == NUM_PIECES; }

int chooseAI(int p) {
    int best = legalMoves[0], bestScore = -9999, opp = 1 - p;
    for (int m = 0; m < numLegal; m++) {
        int idx  = legalMoves[m];
        int dest = getTarget(p, idx, curRoll);
        int score = dest;
        if (dest == PATH_LEN)              score += 200;
        if (dest >= 0 && isRosette(dest))  score +=  50;
        for (int i = 0; i < NUM_PIECES; i++)
            if (players[opp].pos[i] == dest) score += 80;
        if (players[p].pos[idx] == -1)    score -=  10;
        if (score > bestScore) { bestScore = score; best = idx; }
    }
    return best;
}

void initGame() {
    for (int p = 0; p < 2; p++) {
        players[p].finished = 0;
        for (int i = 0; i < NUM_PIECES; i++) players[p].pos[i] = -1;
    }
    players[0].name = "P1"; players[0].color = TFT_BLUE; players[0].isAI = false;
    players[1].name = "P2"; players[1].color = TFT_RED;  players[1].isAI = (gMode == GM_PvAI);
    curPlayer = 0; extraTurn = false;
    curRoll = 0; diceRolled = false;
    navIdx = 0; numLegal = 0; aiPending = false;
    gState = GS_ROLL;
}

void endTurn() {
    if (!extraTurn) curPlayer = 1 - curPlayer;
    extraTurn = false; navIdx = 0; diceRolled = false;
    gState = players[curPlayer].isAI ? GS_AI_MOVE : GS_ROLL;
}

// ============================================================================
//  DRAWING
// ============================================================================

// ── Rosette ornament: circle + 8 radiating spokes (original style) ───────────
void drawRosette(int x, int y) {
    int cx = x + SQ/2, cy = y + SQ/2, r = SQ/4 + 4;
    sprite.drawCircle(cx, cy, r, TFT_RED);
    for (int i = 0; i < 8; i++) {
        float a = i * 45.0f * PI / 180.0f;
        sprite.drawLine(cx, cy,
                        cx + (int)(r * cosf(a)),
                        cy + (int)(r * sinf(a)), TFT_YELLOW);
    }
}

// ── 20-square board grid + rosette ornaments ─────────────────────────────────
void drawBoard() {
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 8; col++) {
            if ((row == 0 || row == 2) && (col == 4 || col == 5)) continue; // gap
            sprite.drawRect(BX + col*SQ, BY + row*SQ, SQ, SQ, TFT_WHITE);
        }
    }
    for (int i = 0; i < 5; i++)
        drawRosette(BX + ROSETTE_CELLS[i].col*SQ, BY + ROSETTE_CELLS[i].row*SQ);
}

// ── 4 tetrahedral dice as triangles on the right edge ────────────────────────
//    Green filled = marked face (scores 1).  White outline = blank face.
//    No text — shape alone conveys the result.
void drawDice() {
    const int baseX = 311, baseY = BY + 4, edge = 17, h = 15, gap = 6;
    for (int i = 0; i < 4; i++) {
        int x1 = baseX,           y1 = baseY + i*(h+gap);
        int x2 = baseX - edge/2,  y2 = y1 + h;
        int x3 = baseX + edge/2,  y3 = y1 + h;
        if (diceVals[i])
            sprite.fillTriangle(x1,y1, x2,y2, x3,y3, TFT_GREEN);
        sprite.drawLine(x1,y1, x2,y2, TFT_WHITE);
        sprite.drawLine(x2,y2, x3,y3, TFT_WHITE);
        sprite.drawLine(x3,y3, x1,y1, TFT_WHITE);
    }
}

// ── One piece: outer filled circle, yellow inner ring when selected ───────────
//    Radius reduced to SQ/2-7 (~10 px) so pieces sit inside the cell clearly.
void drawPiece(int row, int col, uint16_t color, bool selected) {
    int cx = BX + col*SQ + SQ/2;
    int cy = BY + row*SQ + SQ/2;
    sprite.fillCircle(cx, cy, SQ/2 - 7, color);
    if (selected)
        sprite.fillCircle(cx, cy, SQ/2 - 12, TFT_YELLOW);
}

// ── Home / finished counts in the two GAP areas ───────────────────────────────
//    Row 2 gap = P1 (blue).  Row 0 gap = P2 (red).
//    Iterates pieces by index so the currently-selected home piece can be
//    highlighted with a small yellow dot.
void drawStatus() {
    // Which piece index is currently selected (only relevant during selection)?
    int selPieceIdx = (gState == GS_SELECT_PIECE && numLegal > 0)
                      ? legalMoves[navIdx] : -1;

    for (int p = 0; p < 2; p++) {
        int gapRow = (p == 0) ? 2 : 0;
        int gx = BX + 4*SQ + 3;
        int gy = BY + gapRow*SQ + 5;

        const int R = 4, COLS = 4;
        int dotIdx = 0;   // counts home dots drawn so far for this player
        for (int i = 0; i < NUM_PIECES; i++) {
            if (players[p].pos[i] != -1) continue;   // not at home

            int ix = gx + (dotIdx % COLS)*(R*2 + 3) + R;
            int iy = gy + (dotIdx / COLS)*(R*2 + 3) + R;
            sprite.fillCircle(ix, iy, R, players[p].color);

            // Yellow selection dot on this home piece if it is the chosen one
            if (p == curPlayer && i == selPieceIdx)
                sprite.fillCircle(ix, iy, R - 2, TFT_YELLOW);

            dotIdx++;
        }

        if (players[p].finished > 0) {
            sprite.setTextColor(TFT_WHITE, TFT_BLACK);
            sprite.setTextSize(1);
            char buf[5]; snprintf(buf, sizeof(buf), "F:%d", players[p].finished);
            sprite.setCursor(gx + 52, gy + 16);
            sprite.print(buf);
        }
    }
}

// ── Active-player turn line ───────────────────────────────────────────────────
//    3-px bar spanning the full width.
//    P1 (blue) → bottom edge (y 167-169).
//    P2 (red)  → top    edge (y 0-2).
//    Shown only while a human player is in GS_ROLL or GS_SELECT_PIECE;
//    disappears automatically once endTurn() changes state.
void drawTurnLine() {
    if (players[curPlayer].isAI) return;
    if (gState != GS_ROLL && gState != GS_SELECT_PIECE) return;
    uint16_t col = players[curPlayer].color;
    if (curPlayer == 0) sprite.fillRect(0, 167, 320, 3, col);   // bottom → P1
    else                sprite.fillRect(0,   0, 320, 3, col);   // top    → P2
}

// ── NAV-button indicator (top-left corner) ────────────────────────────────────
//    Hidden when it is the AI's turn.
//    GS_ROLL        → small filled circle in the current player's colour.
//    GS_SELECT_PIECE → "n/m" text (cursor / legal-move count) in player colour.
void drawNavIndicator() {
    if (players[curPlayer].isAI) return;
    if (gState != GS_ROLL && gState != GS_SELECT_PIECE) return;
    uint16_t col = players[curPlayer].color;
    if (gState == GS_ROLL) {
        sprite.fillCircle(7, 14, 4, col);
    } else if (numLegal > 0) {
        char buf[6]; snprintf(buf, sizeof(buf), "%d/%d", navIdx + 1, numLegal);
        sprite.setTextSize(1);
        sprite.setTextColor(col, TFT_BLACK);
        sprite.setCursor(2, 10);
        sprite.print(buf);
    }
}

// ── ACT-button label (bottom-left corner) ─────────────────────────────────────
//    Shows "roll dice" before the roll, "move" during piece selection.
void drawActLabel() {
    if (gState != GS_ROLL && gState != GS_SELECT_PIECE) return;
    const char* label = (gState == GS_ROLL) ? "roll dice" : "move";
    sprite.setTextSize(1);
    sprite.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    sprite.setCursor(2, 157);
    sprite.print(label);
}

// ── Full board repaint ────────────────────────────────────────────────────────
void redraw() {
    sprite.fillSprite(TFT_BLACK);
    drawBoard();
    drawStatus();

    // Which piece is highlighted for selection?
    int highlightIdx = (gState == GS_SELECT_PIECE && numLegal > 0)
                       ? legalMoves[navIdx] : -1;

    // All on-board pieces
    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < NUM_PIECES; i++) {
            int pos = players[p].pos[i];
            if (pos < 0 || pos >= PATH_LEN) continue;
            const Coord& c = pathCoord(p, pos);
            drawPiece(c.row, c.col, players[p].color, (p == curPlayer && i == highlightIdx));
        }
    }

    if (diceRolled) drawDice();

    // Overlay UI elements — no text on the board area
    drawTurnLine();
    drawNavIndicator();
    drawActLabel();

    sprite.pushSprite(0, 0);
}

// ── Mode-select screen ────────────────────────────────────────────────────────
void drawModeSelect() {
    sprite.fillSprite(TFT_BLACK);

    sprite.setTextColor(TFT_YELLOW, TFT_BLACK);
    sprite.setTextSize(2);
    sprite.setCursor(18, 6);
    sprite.print("ROYAL  GAME  OF  UR");

    const char* labels[2] = {"  Player vs Player", "  Player vs AI"};
    for (int i = 0; i < 2; i++) {
        bool     sel = (menuSel == i);
        uint16_t bg  = sel ? 0x0340 : 0x2104;     // dark green : dark grey
        uint16_t bdr = sel ? TFT_GREEN : TFT_DARKGREY;
        uint16_t fg  = sel ? TFT_WHITE : TFT_LIGHTGREY;
        sprite.fillRect(16, 46 + i*46, 288, 36, bg);
        sprite.drawRect(16, 46 + i*46, 288, 36, bdr);
        sprite.setTextColor(fg, bg);
        sprite.setTextSize(2);
        sprite.setCursor(24, 55 + i*46);
        sprite.print(labels[i]);
        if (sel)
            sprite.fillTriangle(18, 64+i*46, 18, 74+i*46, 27, 69+i*46, TFT_GREEN);
    }

    sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sprite.setTextSize(1);
    sprite.setCursor(60, 142);
    sprite.print("NAV: cycle    ACT: start");

    sprite.pushSprite(0, 0);
}

// ── Game-over screen ──────────────────────────────────────────────────────────
void drawGameOver() {
    sprite.fillSprite(TFT_BLACK);

    sprite.fillRect(0, 0, 320, 38, players[curPlayer].color);
    sprite.setTextColor(TFT_WHITE, players[curPlayer].color);
    sprite.setTextSize(2);
    sprite.setCursor(80, 10);
    sprite.print("*  WINNER!  *");

    sprite.setTextColor(players[curPlayer].color, TFT_BLACK);
    sprite.setTextSize(3);
    sprite.setCursor(110, 55);
    sprite.print(players[curPlayer].name);

    sprite.setTextColor(TFT_WHITE, TFT_BLACK);
    sprite.setTextSize(2);
    char buf[28];
    snprintf(buf, sizeof(buf), "P1: %d/7    P2: %d/7",
             players[0].finished, players[1].finished);
    sprite.setCursor(22, 110);
    sprite.print(buf);

    sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sprite.setTextSize(1);
    sprite.setCursor(82, 152);
    sprite.print("ACT: play again");

    sprite.pushSprite(0, 0);
}

// ============================================================================
//  AI TURN  (non-blocking)
// ============================================================================
void startAITurn() {
    curRoll    = rollDice();
    diceRolled = true;
    computeLegal(curPlayer, curRoll);
    redraw();
    aiPending = true;
    aiTimer   = millis();
}

void completeAITurn() {
    aiPending = false;
    if (curRoll == 0 || numLegal == 0) {
        delay(1100);
        extraTurn = false;
        endTurn();
    } else {
        int idx = chooseAI(curPlayer);
        doMove(curPlayer, idx);
        redraw();
        delay(900);
        if (checkWin(curPlayer)) { gState = GS_GAME_OVER; drawGameOver(); return; }
        endTurn();
    }
    if (gState == GS_ROLL)    redraw();
    if (gState == GS_AI_MOVE) startAITurn();
}

// ============================================================================
//  BUTTON DEBOUNCE
// ============================================================================
bool btnPressed(int pin) {
    static unsigned long t[2] = {0, 0};
    int idx = (pin == PIN_BUTTON_NAV) ? 0 : 1;
    if (digitalRead(pin) == LOW && millis() - t[idx] > 230) {
        t[idx] = millis(); return true;
    }
    return false;
}

// ============================================================================
//  ARDUINO ENTRY POINTS
// ============================================================================
void setup() {
    Serial.begin(115200);
    pinMode(PIN_BUTTON_NAV, INPUT_PULLUP);
    pinMode(PIN_BUTTON_ACT, INPUT_PULLUP);

    tft.init();
    tft.setRotation(3);             // landscape: 320 × 170
    sprite.createSprite(320, 170);  // full-screen double buffer
    tft.fillScreen(TFT_BLACK);

    randomSeed(esp_random());

    menuSel = 0;
    gState  = GS_MODE_SELECT;
    drawModeSelect();
}

void loop() {
    if (aiPending) {
        if (millis() - aiTimer >= AI_DELAY_MS) completeAITurn();
        return;
    }

    bool nav = btnPressed(PIN_BUTTON_NAV);
    bool act = btnPressed(PIN_BUTTON_ACT);
    if (!nav && !act) return;

    switch (gState) {

        case GS_MODE_SELECT:
            if (nav) { menuSel = 1 - menuSel; drawModeSelect(); }
            if (act) { gMode = (menuSel == 0) ? GM_PVP : GM_PvAI; initGame(); redraw(); }
            break;

        case GS_ROLL:
            if (act) {
                curRoll    = rollDice();
                diceRolled = true;
                computeLegal(curPlayer, curRoll);
                navIdx = 0;
                gState = GS_SELECT_PIECE;
                redraw();

                if (curRoll == 0 || numLegal == 0) {
                    delay(2000);
                    extraTurn = false;
                    endTurn();
                    if (gState == GS_ROLL)    redraw();
                    if (gState == GS_AI_MOVE) startAITurn();
                }
            }
            break;

        case GS_SELECT_PIECE:
            if (numLegal == 0) break;
            if (nav) { navIdx = (navIdx + 1) % numLegal; redraw(); }
            if (act) {
                doMove(curPlayer, legalMoves[navIdx]);
                redraw();
                delay(350);
                if (checkWin(curPlayer)) { gState = GS_GAME_OVER; drawGameOver(); break; }
                endTurn();
                if (gState == GS_ROLL)    redraw();
                if (gState == GS_AI_MOVE) startAITurn();
            }
            break;

        case GS_AI_MOVE:
            if (!aiPending) startAITurn();
            break;

        case GS_GAME_OVER:
            if (act) { menuSel = 0; gState = GS_MODE_SELECT; drawModeSelect(); }
            break;
    }
}