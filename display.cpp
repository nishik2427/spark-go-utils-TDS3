#include "display.h"
//#include <M5Unified.h> // M5Unified
#include "LGFX_TDongleS3-v1.h"
#include <algorithm>
#include "config.h"

namespace display {
namespace {

// インスタンスの作成
LGFX_TDongleS3 lcd;

#define kBg TFT_BLACK
#define kFg TFT_WHITE
#define kAccent TFT_CYAN
#define kInTune TFT_GREEN

uint32_t g_lastTunerDrawMs = 0;
constexpr uint32_t kTunerRedrawIntervalMs = 40; 

String g_lastStatusKey; 
String g_lastConnectedKey;
// チューナーのちらつき防止用キャッシュ
String g_lastTunerNote;
int16_t g_lastNeedleX = -1;
bool g_lastHasSignal = false;

constexpr size_t kCharsPerLine = 21;

// ==========================================
// 変更前: void printWrapped(const String& text, int16_t x, int16_t y, int16_t lineHeight, size_t maxLines) { ... }
// 変更後: 以下の通り、引数を最後まで定義します（デフォルト値も設定）
size_t printWrapped(const String& text, int16_t x, int16_t y, int16_t lineHeight, size_t maxLines, uint8_t textSize = 1, int16_t maxPixelWidth = -1, size_t startIndex = 0) {
    lcd.setTextSize(textSize);
    
    // ピクセル制限に基づく文字数計算
    size_t charsPerLine = kCharsPerLine; // もし kCharsPerLine が別で定義されていなければ 26 などを入れてください
    if (maxPixelWidth > 0) {
        charsPerLine = maxPixelWidth / (6 * textSize);
        if (charsPerLine == 0) charsPerLine = 1;
    }

    size_t start = startIndex; 
    for (size_t line = 0; line < maxLines && start < text.length(); ++line) {
        size_t end = start;
        size_t lastSpace = start;

        while (end < text.length() && (end - start) < charsPerLine) {
            if (text[end] == ' ') lastSpace = end;
            ++end;
        }

        size_t breakAt = end;
        if (end < text.length() && lastSpace > start) {
            breakAt = lastSpace;
        }

        lcd.setCursor(x, y + static_cast<int16_t>(line) * lineHeight);
        lcd.print(text.substring(start, breakAt));
        
        start = breakAt;
        
        // 安全対策
        if (start == breakAt && end == breakAt && start < text.length()) {
            start++; 
        }

        while (start < text.length() && text[start] == ' ') {
            ++start;
        }
    }
    return start; 
}
/*
// フォントサイズと描画制限幅（ピクセル）を明示的に指定できるように拡張
void printWrapped(const String& text, int16_t x, int16_t y, int16_t lineHeight, size_t maxLines, uint8_t textSize = 1, int16_t maxPixelWidth = -1) {
    lcd.setTextSize(textSize); // 呼び出し元のサイズに影響されないよう明示的に設定
    
    // ピクセル指定がない場合は従来の文字数制限(kCharsPerLine)を使用
    size_t charsPerLine = kCharsPerLine;
    if (maxPixelWidth > 0) {
        // 標準フォントは1文字あたり「6ピクセル × テキストサイズ」の幅を持つため計算
        charsPerLine = maxPixelWidth / (6 * textSize);
        if (charsPerLine == 0) charsPerLine = 1;
    }

    size_t start = 0;
    for (size_t line = 0; line < maxLines && start < text.length(); ++line) {
        size_t end = start;
        size_t lastSpace = start;
        while (end < text.length() && (end - start) < charsPerLine) {
            if (text[end] == ' ') lastSpace = end;
            ++end;
        }
        size_t breakAt = end;
        if (end < text.length() && lastSpace > start) {
            breakAt = lastSpace;
        }
        lcd.setCursor(x, y + static_cast<int16_t>(line) * lineHeight);
        lcd.print(text.substring(start, breakAt));
        start = breakAt;
        while (start < text.length() && text[start] == ' ') ++start;
    }
}
*/

void printCentered(const String& text, int16_t y) {
    int16_t w = lcd.textWidth(text);
    lcd.setCursor((lcd.width() - w) / 2, y);
    lcd.print(text);
}

} // namespace

void begin() {
    lcd.init();               // 液晶とバックライトの一斉初期化
    lcd.setRotation(1);       // 画面の向きを横向きに設定
    lcd.fillScreen(TFT_BLACK);// 画面全体を黒でクリア
    
    // テキストの描画
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.setTextSize(2);
    lcd.setCursor(10, 10);
    lcd.println("Spark-Go-Utils");
}

void showBootSplash() {
    lcd.fillScreen(kBg);
    lcd.setTextSize(2);
    lcd.setTextColor(kAccent, kBg);
    printCentered("SPARK", 25);
    printCentered("GO", 50);
    
    lcd.setTextSize(1);
    lcd.setTextColor(kFg, kBg);
    printCentered("Bridge", 80);
    
    lcd.setTextColor(TFT_DARKGREY, kBg);
    printCentered("MVave ->", 100);
    printCentered("Spark GO", 112);
}

void showStatus(const String& connectionLine, int currentPatch1Based, const String& lastEventLine) {
    String key = connectionLine + "|" + String(currentPatch1Based) + "|" + lastEventLine;
    if (key == g_lastStatusKey) return; 
    g_lastStatusKey = key;
    
    lcd.fillScreen(kBg);
    printWrapped(connectionLine, 4, 4, 12, 3, 1);
    
    lcd.setTextSize(1);
    lcd.setTextColor(kFg, kBg);
    lcd.setCursor(4, 45);
    if (currentPatch1Based >= 1) {
        lcd.print("Patch ");
        lcd.print(currentPatch1Based);
    } else {
        lcd.print("Patch --");
    }
    
    lcd.drawFastHLine(0, 60, lcd.width(), TFT_DARKGREY);
    printWrapped(lastEventLine, 4, 66, 12, 4, 1);
}

void showConnected(int currentPatch1Based, const String& patchName, const String& lastEventLine) {
    String key = String(currentPatch1Based) + "|" + patchName + "|" + lastEventLine;
    if (key == g_lastConnectedKey) return; 
    g_lastConnectedKey = key;
    
    lcd.fillScreen(kBg);
    
    // ==========================================================
    // 1. プリセット番号を左端に「縦に2桁」で配置 (サイズ5)
    // ==========================================================
    lcd.setTextSize(5); // 1文字: 幅30px × 高さ40px
    lcd.setTextColor(kFg, kBg);
    
    String tensChar = "0"; // 1桁のときの10の位（消したい場合は " " に変更してください）
    String onesChar = "-";
    
    if (currentPatch1Based >= 1) {
        if (currentPatch1Based >= 10) {
            tensChar = String(currentPatch1Based / 10);
        }
        onesChar = String(currentPatch1Based % 10);
    }
    
    // 上段（10の位）を Y=0 から描画
    lcd.setCursor(2, 0); 
    lcd.print(tensChar);
    
    // 下段（1の位）を Y=40 から描画 (40 + 40 = 80px で縦ぴったり)
    lcd.setCursor(2, 40); 
    lcd.print(onesChar);
    
    // ==========================================================
    // 2. プリセット名を右側の広大なスペースに「サイズ3」で超巨大表示
    // ==========================================================
    lcd.setTextColor(kAccent, kBg);
    String displayName = patchName.length() > 0 ? patchName : "(unnamed)";
    
    // 番号の右側エリア（X=36 から右端まで。利用可能幅: 約122ピクセル）
    int16_t rightSideX = 36;
    int16_t rightSideWidth = lcd.width() - rightSideX - 2; // 約122px（サイズ3で6〜7文字）
    
    // Y=4 から 26px間隔（フォント高さ24px + 余裕2px）で最大2行分描画
    size_t nextStart = printWrapped(displayName, rightSideX, 4, 26, 2, 3, rightSideWidth, 0);
    
    // ==========================================================
    // 3. 右側最下部にイベント行を描画 (サイズ1)
    // ==========================================================
    // 名前の描画エリアの下（Y=56）に細い境界線を引き、その下に1行表示
    int16_t eventY = 56;
    lcd.drawFastHLine(rightSideX, eventY, rightSideWidth, TFT_DARKGREY);
    
    printWrapped(lastEventLine, rightSideX, eventY + 4, 10, 2, 1, rightSideWidth, 0);
}

void showTuner(const char* noteName, float cents, bool hasSignal) {
    uint32_t now = millis();
    if (now - g_lastTunerDrawMs < kTunerRedrawIntervalMs) return;
    g_lastTunerDrawMs = now;
    
    // ==========================================================
    // 座標・サイズ定義（メーター最大化設計）
    // ==========================================================
    // 音名：プリセット画面の縦2桁と同じ幅（サイズ5: 幅30px）に収めて左寄せ
    constexpr int16_t kNoteLeft = 2;   
    constexpr int16_t kNoteTop = 12;   
    constexpr int16_t kNoteWidth = 32; 

    // メーター部：横幅を120pxまで極限拡張（画面の約75%がメーターになります）
    constexpr int16_t kBarLeft = 36;   
    constexpr int16_t kBarRight = 156;
    constexpr int16_t kBarTop = 10;    
    constexpr int16_t kBarH = 44;      
    constexpr int16_t barWidth = kBarRight - kBarLeft;
    constexpr int16_t centerX = kBarLeft + barWidth / 2;

    // 計算
    float clamped = cents < -50 ? -50 : (cents > 50 ? 50 : cents);
    int16_t newNeedleX = centerX + static_cast<int16_t>((clamped / 50.0f) * (barWidth / 2));
    uint16_t needleColor = (clamped > -5 && clamped < 5) ? kInTune : kAccent;

    // 状態変化チェック
    if (g_lastHasSignal == hasSignal && g_lastTunerNote == noteName && g_lastNeedleX == newNeedleX) {
        return;
    }

    // 初回、または信号状態が変わった場合は背景とメーター枠をリセット
    if (g_lastNeedleX == -1 || g_lastHasSignal != hasSignal) {
        lcd.fillScreen(kBg);
        lcd.drawRect(kBarLeft, kBarTop, barWidth, kBarH, TFT_DARKGREY);
    } else {
        // 前回の針を消去
        if (g_lastNeedleX >= kBarLeft + 1 && g_lastNeedleX <= kBarRight - 2) {
            lcd.fillRect(g_lastNeedleX - 1, kBarTop + 1, 3, kBarH - 2, kBg);
        }
    }
    /*
    // 1. 音名の描画（サイズ5ですっきりと配置）
    if (g_lastTunerNote != noteName || g_lastHasSignal != hasSignal) {
        lcd.fillRect(kNoteLeft, kNoteTop, kNoteWidth, 40, kBg); 
        lcd.setTextSize(5); 
        lcd.setTextColor(hasSignal ? kFg : TFT_DARKGREY, kBg);
        lcd.setCursor(kNoteLeft, kNoteTop);
        lcd.print(hasSignal ? noteName : "--");
        g_lastTunerNote = noteName;
    }
    */
    // 1. 音名の描画（サイズ5ですっきりと配置）
    if (g_lastTunerNote != noteName || g_lastHasSignal != hasSignal) {
        lcd.fillRect(kNoteLeft, kNoteTop, kNoteWidth, 40, kBg); 
        lcd.setTextSize(5); 
        lcd.setTextColor(hasSignal ? kFg : TFT_DARKGREY, kBg);
        lcd.setCursor(kNoteLeft, kNoteTop);
        lcd.print(hasSignal ? noteName[0] : '-');
        // 2文字目に '#' や 'b' があるかチェック
        if (noteName[0] != '\0' && noteName[1] != '\0') {
            // 右上の少し高い位置（Topから-4pxなど）にサイズ3で小さく描画
            lcd.setTextSize(3);
            lcd.setCursor(kNoteLeft + 30, kNoteTop - 2); 
            lcd.print(noteName[1]);
        }
        
        g_lastTunerNote = noteName;
    }
    
    // 2. メーター中央線（ジャストピッチ位置）の再描画
    lcd.drawFastVLine(centerX, kBarTop + 1, kBarH - 2, TFT_DARKGREY);

    // 3. 新しい針の描画（幅3ピクセルでクッキリ表示）
    if (hasSignal) {
        lcd.fillRect(newNeedleX - 1, kBarTop + 1, 3, kBarH - 2, needleColor);
        g_lastNeedleX = newNeedleX;
    } else {
        g_lastNeedleX = -1;
    }
    
    // 4. 下部イベント・ステータスエリア (Y=64〜)
    lcd.drawFastHLine(0, 64, lcd.width(), TFT_DARKGREY);
    if (hasSignal) {
        // メーターの下に cents の詳細数値を表示
        lcd.setTextSize(1);
        lcd.setTextColor(needleColor, kBg);
        lcd.setCursor(kBarLeft, 68);
        lcd.printf("%+0.1f cents", cents);
    }
    
    g_lastHasSignal = hasSignal;
}


void invalidate() {
    g_lastStatusKey = "\x01invalid\x01";
    g_lastConnectedKey = "\x01invalid\x01";
    g_lastTunerNote = "";
    g_lastNeedleX = -1;
    g_lastTunerDrawMs = 0;
}

void drawDebugMessage(const String& msg) {
    // T-Dongle-S3????????????????????????
    // ????????(y)?????????????????
    lcd.setTextSize(2);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK); // ???????????????
    
    // ?????????y=64????????????????????????????
    // T-Dongle-S3?????????? 160x80 ??
    //lcd.fillRect(0, 68, 160, 12, TFT_BLACK); 
    //lcd.setCursor(0, 68);
    lcd.fillRect(0, 0, 160, 80, TFT_BLACK); 
    lcd.setCursor(10, 10);
    lcd.print(msg.substring(0, 26)); // ???????????????
}

} // namespace display
