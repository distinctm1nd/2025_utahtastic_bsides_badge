// Multi-tap text editor used by the badge's message editor and tagline editor.
// Pure logic (no Arduino/Meshtastic), tested in tools/keypad_test.cpp.
//
//   digits     tap for letters, tap again within a second for the next letter; hold = digit
//   0          space (tap twice for 0)      1  punctuation . , ? ! ' - @ : 1
//   *          delete (hold = cancel)       #  done/send (hold = change case)
//   joystick   left/right move the cursor, up = change case, down = new letter right away
// Case modes: "abc", "Abc" (capital after start and after ". ? !"), "ABC".
#pragma once
#include "BadgeKeypadLogic.h"
#include <stdint.h>
#include <string.h>

namespace badge
{

class TextEntry
{
  public:
    enum Result { TE_NONE, TE_DONE, TE_CANCEL };
    enum CaseMode { CASE_LOWER, CASE_SENTENCE, CASE_UPPER };
    static const int MAX = 200;
    static const uint32_t COMMIT_MS = 1000;

    void reset(const char *initial = "", int maxLen = MAX, CaseMode mode = CASE_SENTENCE)
    {
        limit = maxLen > MAX ? MAX : maxLen;
        strncpy(buf, initial ? initial : "", (size_t)limit);
        buf[limit] = 0;
        len = (int)strlen(buf);
        cur = len;
        caseMode = mode;
        pendKey = -1;
    }

    const char *text() const { return buf; }
    int length() const { return len; }
    int cursor() const { return cur; }
    CaseMode mode() const { return caseMode; }
    const char *modeName() const { return caseMode == CASE_LOWER ? "abc" : caseMode == CASE_UPPER ? "ABC" : "Abc"; }

    // the letters of the key being tapped (for the strip on screen), or nullptr
    const char *pendingLetters() const { return pendKey >= 0 ? KeypadLogic::lettersFor(pendKey) : nullptr; }
    int pendingIndex() const { return pendIdx; }
    char applyCase(char c) const
    {
        bool up = caseMode == CASE_UPPER || (caseMode == CASE_SENTENCE && sentenceStart());
        return (up && c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }

    // call every frame: a tapped letter is final once the key hasn't been tapped for a second
    void tick(uint32_t now)
    {
        if (pendKey >= 0 && (int32_t)(now - pendAt) >= (int32_t)COMMIT_MS) // signed: now may be a bit older than pendAt
            pendKey = -1;
    }

    Result key(int k, KeyAction a, uint32_t now)
    {
        if (a == KA_RELEASE) {
            if (k == AK_STAR && !held[0])
                backspace();
            if (k == AK_HASH && !held[1]) {
                pendKey = -1;
                return TE_DONE;
            }
            if (k == AK_STAR)
                held[0] = false;
            if (k == AK_HASH)
                held[1] = false;
            return TE_NONE;
        }
        if (a == KA_HOLD) {
            if (k == AK_STAR) {
                held[0] = true;
                return TE_CANCEL;
            }
            if (k == AK_HASH) {
                held[1] = true;
                cycleCase();
                return TE_NONE;
            }
            if (appKeyDigit(k) >= 0 && pendKey == k) { // held: the digit instead of the letter
                buf[cur - 1] = KeypadLogic::digitFor(k);
                pendKey = -1;
            }
            return TE_NONE;
        }
        // press or repeat
        switch (k) {
        case AK_LEFT:
            pendKey = -1;
            if (cur > 0)
                cur--;
            return TE_NONE;
        case AK_RIGHT:
            pendKey = -1;
            if (cur < len)
                cur++;
            return TE_NONE;
        case AK_UP:
            if (a == KA_PRESS)
                cycleCase();
            return TE_NONE;
        case AK_DOWN:
            pendKey = -1;
            return TE_NONE;
        case AK_STAR:
        case AK_HASH:
            return TE_NONE; // act on release/hold
        default:
            break;
        }
        if (a != KA_PRESS)
            return TE_NONE;
        const char *letters = KeypadLogic::lettersFor(k);
        if (!letters)
            return TE_NONE;
        if (pendKey == k && (int32_t)(now - pendAt) < (int32_t)COMMIT_MS && cur > 0) {
            pendIdx = letters[pendIdx + 1] ? pendIdx + 1 : 0;
            buf[cur - 1] = caseAtPend ? upper(letters[pendIdx]) : letters[pendIdx];
        } else {
            pendIdx = 0;
            caseAtPend = applyCase('a') == 'A';
            if (!insert(caseAtPend ? upper(letters[0]) : letters[0])) {
                pendKey = -1;
                return TE_NONE;
            }
        }
        pendKey = k;
        pendAt = now;
        return TE_NONE;
    }

  private:
    char buf[MAX + 1] = {};
    int len = 0, cur = 0, limit = MAX;
    CaseMode caseMode = CASE_SENTENCE;
    int pendKey = -1, pendIdx = 0;
    bool caseAtPend = false;
    uint32_t pendAt = 0;
    bool held[2] = {false, false};

    static char upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

    bool sentenceStart() const
    {
        int i = cur - 1;
        while (i >= 0 && buf[i] == ' ')
            i--;
        return i < 0 || buf[i] == '.' || buf[i] == '?' || buf[i] == '!';
    }
    void cycleCase()
    {
        pendKey = -1;
        caseMode = (CaseMode)((caseMode + 1) % 3);
    }
    bool insert(char c)
    {
        if (len >= limit)
            return false;
        memmove(buf + cur + 1, buf + cur, (size_t)(len - cur + 1));
        buf[cur++] = c;
        len++;
        return true;
    }
    void backspace()
    {
        pendKey = -1;
        if (cur == 0)
            return;
        memmove(buf + cur - 1, buf + cur, (size_t)(len - cur + 1));
        cur--;
        len--;
    }
};

} // namespace badge
