// Hardware-style programming: press a module key, type the function number on the keypad and the
// LCD walks you through it, exactly like the front panel of the original machine.
#pragma once

#include "Engine.h"

#include <functional>
#include <string>

class Crate12Editor;

class FrontPanel
{
public:
    enum class Module { None, Setup, Special, Sync, Disk, Sample, SegLength, Step, Copy };
    enum Key { K0 = 0, K9 = 9, Yes = 10, No = 11, Enter = 12, Left = 13, Right = 14 };

    explicit FrontPanel (Crate12Editor& e) : ed (e) {}

    void start (Module m);
    void key (int k);
    bool pad (int pad, bool eraseHeld);     // true = consumed by the current function
    bool slider (int index, float value);   // true = consumed
    bool sliderPositions (float* v8) const;  // when a function owns the sliders
    bool active() const { return mod != Module::None; }
    Module module() const { return mod; }
    void cancel (const char* why = nullptr);

private:
    c12::Engine& eng();
    void show (const std::string& a, const std::string& b);
    void prompt();
    void enterFunction();
    void done (const std::string& a, const std::string& b = {});
    int  digitsNeeded() const;
    std::string soundName (int s);
    void showStep();
    void showTruncate();

    Crate12Editor& ed;
    Module mod = Module::None;
    int fn = -1, stage = 0, argA = -1, argB = -1;
    std::string digits;
    // SET-UP 19 truncate/loop sliders
    float sl[6] {};
    // SPECIAL 21 naming
    std::string nameBuf; int cursor = 0;
    // step program
    int stepTick = 0;
};
