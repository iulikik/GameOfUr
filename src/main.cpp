#include <Arduino.h>
#include "TFT_eSPI.h"

#define PIN_BUTTON_1 0 // Button to cycle through pieces
#define PIN_BUTTON_2 14 // Action button

// TFT setup
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

// Board setup
const int BOARD_ROWS = 3;
const int BOARD_COLS = 8;
int board[BOARD_ROWS][BOARD_COLS] = {0}; // Tracks pieces on the board

// Define finish tiles
const int finishTileP1[2] = {2, 5};
const int finishTileP2[2] = {0, 5};

// Define rosette tiles
const int rosettes[5][2] = {{0, 0}, {0, 6}, {1, 3}, {2, 0}, {2, 6}};

// Function to check if a piece is on a finish tile
bool isOnFinishTile(int player, int row, int col) {
    return (player == 1 && row == finishTileP1[0] && col == finishTileP1[1]) ||
           (player == 2 && row == finishTileP2[0] && col == finishTileP2[1]);
}

// Function to check if a piece is on a rosette tile
bool isOnRosette(int row, int col) {
    for (int i = 0; i < 5; i++) {
        if (rosettes[i][0] == row && rosettes[i][1] == col) {
            return true;
        }
    }
    return false;
}

// Dice values
int diceValues[4];  // Global array to store dice values
int totalMovement = 0; // Tracks total movement based on dice rolls

// Player pieces
int player1[7] = {0, 0, 0, 0, 0, 0, 0};
int player2[7] = {0, 0, 0, 0, 0, 0, 0};

// Player 1's path coordinates
struct Coordinate {
    int row;
    int col;
};
Coordinate player1Path[] = {
    {3, 5}, {3, 4}, {3, 3}, {3, 2}, {3, 1}, 
    {2, 1}, {2, 2}, {2, 3}, {2, 4}, {2, 5}, 
    {2, 6}, {2, 7}, {2, 8}, {3, 8}, {3, 7}, 
    {3, 6}
};

// Player 2's path coordinates
Coordinate player2Path[] = {
    {1, 5}, {1, 4}, {1, 3}, {1, 2}, {1, 1}, 
    {2, 1}, {2, 2}, {2, 3}, {2, 4}, {2, 5}, 
    {2, 6}, {2, 7}, {2, 8}, {1, 8}, {1, 7}, 
    {1, 6}
};

int currentPieceIndex = 0; // Tracks the currently selected piece
bool isPlayer1Turn = true; // Tracks whose turn it is

void setup() {
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);

    tft.init();
    tft.setRotation(3);
    sprite.createSprite(360, 240);

    Serial.begin(115200);  // Start serial communication at 115200 baud
    Serial.println("Game started!");  // Optional start-up message

    // Initialize board pieces
    board[2][4] = 1; // Player 1 piece
    board[0][4] = 2; // Player 2 piece
}

void drawRosette(int x, int y, int squareSize) {
    int centerX = x + squareSize / 2;
    int centerY = y + squareSize / 2;
    int radius = squareSize / 4 + 4;

    sprite.drawCircle(centerX, centerY, radius, TFT_RED);
    for (int i = 0; i < 8; i++) {
        float angle = i * 45 * PI / 180;
        int x1 = centerX + radius * cos(angle);
        int y1 = centerY + radius * sin(angle);
        sprite.drawLine(centerX, centerY, x1, y1, TFT_YELLOW);
    }
}

void drawBoard() {
    int squareSize = 35;
    for (int i = 0; i < BOARD_ROWS; i++) {
        for (int j = 0; j < BOARD_COLS; j++) {
            if ((i == 0 || i == 2) && (j < 4 || j > 5)) {
                int x = 10 + j * squareSize;
                int y = 30 + i * squareSize;
                sprite.drawRect(x, y, squareSize, squareSize, TFT_WHITE);
                if (j == 0 || j == 6) {
                    drawRosette(x, y, squareSize);
                }
            } else if (i == 1) {
                int x = 10 + j * squareSize;
                int y = 30 + i * squareSize;
                sprite.drawRect(x, y, squareSize, squareSize, TFT_WHITE);
                if (j == 3) {
                    drawRosette(x, y, squareSize);
                }
            }
        }
    }
}

int* rollDice() {
    Serial.printf("P%d rolling dice...\n", isPlayer1Turn ? 1 : 2);  // Log rolling action
    for (int i = 0; i < 4; i++) {
        diceValues[i] = random(0, 4);  // Random number between 0 and 3
    }
    int total = 0;
    for (int i = 0; i < 4; i++) {
        if (diceValues[i] == 1 || diceValues[i] == 3) {
            total++;
        }
    }
    Serial.printf("P%d rolled %d\n", isPlayer1Turn ? 1 : 2, total);  // Log dice result
    return diceValues;
}

void drawMessage(const char* message) {
    // Clear the message area
    sprite.fillRect(10, 5, 300, 20, TFT_BLACK); // Adjust the rectangle dimensions as needed

    // Draw the new message
    sprite.setTextColor(TFT_WHITE);
    sprite.setTextSize(2);
    sprite.setCursor(10, 5);
    sprite.print(message);
}

void drawDice(int values[]) {
    int baseX = 305;           // X position near the right edge of the screen
    int baseY = 35;            // Starting Y position for the first triangle
    int edgeLength = 18;       // Edge length for equilateral triangles
    int height = 15;           // Height of equilateral triangle (precalculated)
    int spacing = 10;          // Spacing between triangles
    int greenCount = 0;        // Variable to count how many dice are green
    for (int i = 0; i < 4; i++) {
        int x1 = baseX;                                     // Top vertex
        int y1 = baseY + i * (height + spacing);
        int x2 = baseX - edgeLength / 2;                    // Bottom-left vertex
        int y2 = y1 + height;
        int x3 = baseX + edgeLength / 2;                    // Bottom-right vertex
        int y3 = y1 + height;
        // Draw the outline of the triangle
        sprite.drawLine(x1, y1, x2, y2, TFT_WHITE);  // Top to bottom-left
        sprite.drawLine(x2, y2, x3, y3, TFT_WHITE);  // Bottom-left to bottom-right
        sprite.drawLine(x3, y3, x1, y1, TFT_WHITE);  // Bottom-right to top
        // Fill the triangle with green color if the rolled value is 1 or 3
        if (values[i] == 1 || values[i] == 3) {
            sprite.fillTriangle(x1, y1, x2, y2, x3, y3, TFT_GREEN);
            greenCount++;  // Increment the green count
        }
    }
    // Draw the roll result message
    char rollMessage[30];
    snprintf(rollMessage, sizeof(rollMessage), "P%d rolled %d", isPlayer1Turn ? 1 : 2, greenCount);
    drawMessage(rollMessage);
}

void drawPiece(int row, int col, uint16_t outerColor, bool isSelected) {
    int squareSize = 35;
    int startX = -25; // Adjust startX as needed
    int startY = -5;  // Adjust startY as needed
    // Calculate circle position
    int x = startX + col * squareSize + squareSize / 2;
    int y = startY + row * squareSize + squareSize / 2;
    // Draw the piece
    sprite.fillCircle(x, y, squareSize / 4 + 5, outerColor);
    // If the piece is selected, draw a yellow inner circle
    if (isSelected) {
        sprite.fillCircle(x, y, squareSize / 4, TFT_YELLOW);
    }
}

void handlePieceSelection() {
    if (digitalRead(PIN_BUTTON_1) == LOW) {
        currentPieceIndex = (currentPieceIndex + 1) % 7;
        delay(200);
    }
}

void handleAction() {
    if (digitalRead(PIN_BUTTON_2) == LOW) {
        if (totalMovement > 0) {
            int* playerPieces = isPlayer1Turn ? player1 : player2;
            int piecePosition = playerPieces[currentPieceIndex];

            if (piecePosition + totalMovement <= 15) {
                playerPieces[currentPieceIndex] += totalMovement;
                totalMovement = 0;
                isPlayer1Turn = !isPlayer1Turn;
            }
        }
        delay(200);
    }
}

void logBoardState() {
    Serial.println("Board State:");
    for (int i = 0; i < BOARD_ROWS; i++) {
        for (int j = 0; j < BOARD_COLS; j++) {
            Serial.print(board[i][j]);
            Serial.print(" ");
        }
        Serial.println();
    }
}

void updateBoardState() {
    // Clear the board
    for (int i = 0; i < BOARD_ROWS; i++) {
        for (int j = 0; j < BOARD_COLS; j++) {
            board[i][j] = 0;
        }
    }

    // Update with player 1 pieces
    for (int i = 0; i < 7; i++) {
        if (player1[i] > 0) {
            int row = player1[i] / BOARD_COLS;
            int col = player1[i] % BOARD_COLS;
            board[row][col] = 1; // Player 1 piece
        }
    }

    // Update with player 2 pieces
    for (int i = 0; i < 7; i++) {
        if (player2[i] > 0) {
            int row = player2[i] / BOARD_COLS;
            int col = player2[i] % BOARD_COLS;
            if (board[row][col] == 1) {
                board[row][col] = 3; // Conflict state
            } else {
                board[row][col] = 2; // Player 2 piece
            }
        }
    }
}

void loop() {
    static bool needsUpdate = true;   // Track if the screen needs to be updated
    static bool diceRolled = false;  // Whether the dice have been rolled
    static int selectedPiece = 0;    // Currently selected piece
    static int totalMovement = 0;    // Total dice roll result

    // Handle button 1: Select the next piece
    if (digitalRead(PIN_BUTTON_1) == LOW) {
        selectedPiece = (selectedPiece + 1) % 7;
        Serial.printf("P%d selected piece %d\n", isPlayer1Turn ? 1 : 2, selectedPiece);
        needsUpdate = true;
        delay(200); // Debounce delay
    }

    // Handle button 2: Roll dice or move a piece
    if (digitalRead(PIN_BUTTON_2) == LOW) {
        if (!diceRolled) {
            // Roll dice if not already rolled
            int* rolledValues = rollDice();
            totalMovement = 0;
            for (int i = 0; i < 4; i++) {
                if (rolledValues[i] == 1 || rolledValues[i] == 3) {
                    totalMovement++;
                }
            }
            diceRolled = true;  // Mark dice as rolled
        } else {
            // Move the selected piece if dice have been rolled
            int* playerPieces = isPlayer1Turn ? player1 : player2;
            int& position = playerPieces[selectedPiece];

            int startPosition = position;  // Record starting position
            position += totalMovement;

            if (position > 15) position = 15;  // Cap position to max path length

            Serial.printf("P%d moved piece %d from [%d][%d] to [%d][%d]\n",
                          isPlayer1Turn ? 1 : 2,
                          selectedPiece,
                          isPlayer1Turn ? player1Path[startPosition].row : player2Path[startPosition].row,
                          isPlayer1Turn ? player1Path[startPosition].col : player2Path[startPosition].col,
                          isPlayer1Turn ? player1Path[position].row : player2Path[position].row,
                          isPlayer1Turn ? player1Path[position].col : player2Path[position].col);

            Serial.printf("P%d ended their move\n", isPlayer1Turn ? 1 : 2);

            // Reset movement and dice state for the next turn
            totalMovement = 0;
            diceRolled = false;
            isPlayer1Turn = !isPlayer1Turn;  // Switch turns
        }
        needsUpdate = true;
        delay(200); // Debounce delay
    }

    // Update the screen if needed
    if (needsUpdate) {
        sprite.fillSprite(TFT_BLACK);  // Clear the screen
        updateBoardState();
        logBoardState();

        // Draw the game board
        drawBoard();

        // Display the turn or roll result message
        if (!diceRolled) {
            char turnMessage[20];
            snprintf(turnMessage, sizeof(turnMessage), "P%d's turn", isPlayer1Turn ? 1 : 2);
            drawMessage(turnMessage);
        } else {
            char rollMessage[30];
            snprintf(rollMessage, sizeof(rollMessage), "P%d rolled %d", isPlayer1Turn ? 1 : 2, totalMovement);
            drawMessage(rollMessage);
        }

        // Draw dice
        if (diceRolled) {
            drawDice(diceValues);
        }

        // Draw player pieces
        for (int i = 0; i < 7; i++) {
            drawPiece(player1Path[player1[i]].row, player1Path[player1[i]].col, TFT_BLUE, isPlayer1Turn && selectedPiece == i);
            drawPiece(player2Path[player2[i]].row, player2Path[player2[i]].col, TFT_RED, !isPlayer1Turn && selectedPiece == i);
        }

        // Push sprite to display
        sprite.pushSprite(0, 0);
        needsUpdate = false;
    }
}