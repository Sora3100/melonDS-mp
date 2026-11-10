/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <algorithm>
#include <string>
#include <cstring>
#include <QKeyEvent>
#include <QMetaObject>
#include <SDL2/SDL.h>

#include "Platform.h"
#include "SDL_gamecontroller.h"
#include "SDL_sensor.h"
#include "main.h"
#include "Config.h"
#include "InputConfig/InputConfigDialog.h"

using namespace melonDS;

const char* EmuInstance::buttonNames[EmuInstance::numButtons] =
{
    "A",
    "B",
    "Select",
    "Start",
    "Right",
    "Left",
    "Up",
    "Down",
    "R",
    "L",
    "X",
    "Y",
    "L2",
    "R2",
    "L3",
    "R3",
    "LStickRight",
    "LStickLeft",
    "LStickUp",
    "LStickDown",
    "RStickRight",
    "RStickLeft",
    "RStickUp",
    "RStickDown"
};

static constexpr melonDS::u32 kAllReleased = 0xFFFFFF;

const char* EmuInstance::hotkeyNames[HK_MAX] =
{
    "HK_Lid",
    "HK_Mic",
    "HK_Pause",
    "HK_Reset",
    "HK_FastForward",
    "HK_FrameLimitToggle",
    "HK_FullscreenToggle",
    "HK_SwapScreens",
    "HK_SwapScreenEmphasis",
    "HK_SolarSensorDecrease",
    "HK_SolarSensorIncrease",
    "HK_FrameStep",
    "HK_PowerButton",
    "HK_VolumeUp",
    "HK_VolumeDown",
    "HK_AudioMuteToggle",
    "HK_SlowMo",
    "HK_FastForwardToggle",
    "HK_SlowMoToggle",
    "HK_GuitarGripGreen",
    "HK_GuitarGripRed",
    "HK_GuitarGripYellow",
    "HK_GuitarGripBlue"
};

std::shared_ptr<SDL_mutex> EmuInstance::joyMutexGlobal = nullptr;


void EmuInstance::inputInit()
{
    if (!joyMutexGlobal)
    {
        SDL_mutex* mutex = SDL_CreateMutex();
        joyMutexGlobal = std::shared_ptr<SDL_mutex>(mutex, SDL_DestroyMutex);
    }
    joyMutex = joyMutexGlobal;

    keyInputMask = kAllReleased;
    joyInputMask = kAllReleased;
    inputMask = kAllReleased;
    knownJoysticks.clear();
    for (int i = 0; i < numPads; i++) padHasGUID[i] = false;

    // positional face buttons (SDL "A" = bottom) on every controller, so the
    // auto-mapping below always puts DS A on the right, B on the bottom, etc.
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    memset(padAxes, 0, sizeof(padAxes));
    memset(padTriggers, 0, sizeof(padTriggers));

    for (auto& p : extPads)
    {
        p.joystickID = -1;
        p.joystick = nullptr;
        p.keyInputMask = kAllReleased;
        p.inputMask = kAllReleased;
        p.connected = false;
        memset(p.axes, 0, sizeof(p.axes));
        memset(p.triggers, 0, sizeof(p.triggers));
    }

    keyHotkeyMask = 0;
    joyHotkeyMask = 0;
    hotkeyMask = 0;
    lastHotkeyMask = 0;

    isTouching = false;
    touchX = 0;
    touchY = 0;

    joystick = nullptr;
    controller = nullptr;
    hasRumble = false;
    hasAccelerometer = false;
    hasGyroscope = false;
    isRumbling = false;

    inputLoadConfig();
}

void EmuInstance::inputDeInit()
{
    SDL_LockMutex(joyMutex.get());
    closeJoystick();
    for (auto& p : extPads)
    {
        if (p.joystick) SDL_JoystickClose(p.joystick);
        p.joystick = nullptr;
    }
    SDL_UnlockMutex(joyMutex.get());
}

void EmuInstance::inputLoadConfig()
{
    SDL_LockMutex(joyMutex.get());

    Config::Table keycfg = localCfg.GetTable("Keyboard");
    Config::Table joycfg = localCfg.GetTable("Joystick");

    extendedButtons = localCfg.GetBool("ExtendedButtons");
    bool wasMulti = multiController;
    multiController = localCfg.GetBool("MultiController");
    // multi-controller just turned on: let the hotplug scan hand out the
    // joysticks that are already connected
    if (multiController && !wasMulti)
        knownJoysticks.clear();

    for (int i = 0; i < numButtons; i++)
    {
        keyMapping[i] = keycfg.GetInt(buttonNames[i]);
        joyMapping[i] = joycfg.GetInt(buttonNames[i]);
    }

    for (int p = 1; p < numPads; p++)
    {
        ExtPad& pad = extPads[p-1];
        Config::Table padcfg = localCfg.GetTable("Pad" + std::to_string(p+1));
        Config::Table pkeycfg = padcfg.GetTable("Keyboard");
        Config::Table pjoycfg = padcfg.GetTable("Joystick");
        for (int i = 0; i < numButtons; i++)
        {
            pad.keyMapping[i] = pkeycfg.GetInt(buttonNames[i]);
            pad.joyMapping[i] = pjoycfg.GetInt(buttonNames[i]);
        }
        pad.keyInputMask = kAllReleased;
        pad.joystickID = padcfg.GetInt("JoystickID");
        if (multiController)
            openExtJoystick(p);
        else
        {
            if (pad.joystick) SDL_JoystickClose(pad.joystick);
            pad.joystick = nullptr;
        }
    }

    for (int i = 0; i < HK_MAX; i++)
    {
        hkKeyMapping[i] = keycfg.GetInt(hotkeyNames[i]);
        hkJoyMapping[i] = joycfg.GetInt(hotkeyNames[i]);
    }

    setJoystick(localCfg.GetInt("JoystickID"));
    SDL_UnlockMutex(joyMutex.get());
}

void EmuInstance::inputRumbleStart(melonDS::u32 len_ms)
{
    SDL_LockMutex(joyMutex.get());

    if (controller && hasRumble && !isRumbling)
    {
        SDL_GameControllerRumble(controller, 0xFFFF, 0xFFFF, len_ms);
        isRumbling = true;
    }

    SDL_UnlockMutex(joyMutex.get());
}

void EmuInstance::inputRumbleStop()
{
    SDL_LockMutex(joyMutex.get());

    if (controller && hasRumble && isRumbling)
    {
        SDL_GameControllerRumble(controller, 0, 0, 0);
        isRumbling = false;
    }

    SDL_UnlockMutex(joyMutex.get());
}

float EmuInstance::inputMotionQuery(melonDS::Platform::MotionQueryType type)
{
    float values[3];
    SDL_LockMutex(joyMutex.get());
    if (type <= melonDS::Platform::MotionAccelerationZ)
    {
        if (controller && hasAccelerometer)
        {
            if (SDL_GameControllerGetSensorData(controller, SDL_SENSOR_ACCEL, values, 3) == 0)
            {
                // Map values from DS console orientation to SDL controller orientation.
                SDL_UnlockMutex(joyMutex.get());
                switch (type)
                {
                case melonDS::Platform::MotionAccelerationX:
                    return values[0];
                case melonDS::Platform::MotionAccelerationY:
                    return -values[2];
                case melonDS::Platform::MotionAccelerationZ:
                    return values[1];
                default:
                    break;
                }
            }
        }
    }
    else if (type <= melonDS::Platform::MotionRotationZ)
    {
        if (controller && hasGyroscope)
        {
            if (SDL_GameControllerGetSensorData(controller, SDL_SENSOR_GYRO, values, 3) == 0)
            {
                // Map values from DS console orientation to SDL controller orientation.
                SDL_UnlockMutex(joyMutex.get());
                switch (type)
                {
                case melonDS::Platform::MotionRotationX:
                    return values[0];
                case melonDS::Platform::MotionRotationY:
                    return -values[2];
                case melonDS::Platform::MotionRotationZ:
                    return values[1];
                default:
                    break;
                }
            }
        }
    }
    SDL_UnlockMutex(joyMutex.get());
    if (type == melonDS::Platform::MotionAccelerationZ)
        return SDL_STANDARD_GRAVITY;
    return 0.0f;
}


void EmuInstance::setJoystick(int id)
{
    SDL_LockMutex(joyMutex.get());
    joystickID = id;
    openJoystick();
    SDL_UnlockMutex(joyMutex.get());
}

void EmuInstance::setPadJoystick(int pad, int id)
{
    if (pad == 0) { setJoystick(id); return; }
    SDL_LockMutex(joyMutex.get());
    extPads[pad-1].joystickID = id;
    openExtJoystick(pad);
    SDL_UnlockMutex(joyMutex.get());
}

int EmuInstance::getPadJoystickIndex(int pad)
{
    SDL_LockMutex(joyMutex.get());
    int ret = -1;
    SDL_Joystick* joy = getPadJoystick(pad);
    if (joy)
    {
        SDL_JoystickID id = SDL_JoystickInstanceID(joy);
        int num = SDL_NumJoysticks();
        for (int i = 0; i < num; i++)
            if (SDL_JoystickGetDeviceInstanceID(i) == id) { ret = i; break; }
    }
    SDL_UnlockMutex(joyMutex.get());
    return ret;
}

bool EmuInstance::isJoystickHeld(SDL_JoystickID id)
{
    if (joystick && SDL_JoystickInstanceID(joystick) == id) return true;
    for (auto& p : extPads)
        if (p.joystick && SDL_JoystickInstanceID(p.joystick) == id) return true;
    return false;
}

int EmuInstance::pickFreePad(SDL_JoystickGUID guid)
{
    // 1. the free pad this kind of controller was last unplugged from
    for (int pn = 0; pn < (multiController ? numPads : 1); pn++)
    {
        if (getPadJoystick(pn) || !padHasGUID[pn]) continue;
        if (!memcmp(&padLastGUID[pn], &guid, sizeof(guid))) return pn;
    }
    // 2. the first pad without a joystick
    int maxpad = multiController ? numPads : 1;
    for (int pn = 0; pn < maxpad; pn++)
        if (!getPadJoystick(pn)) return pn;
    return -1;
}

static int bindToMapping(const SDL_GameControllerButtonBind& b, int axisdir)
{
    switch (b.bindType)
    {
    case SDL_CONTROLLER_BINDTYPE_BUTTON:
        return b.value.button & 0xFFFF;
    case SDL_CONTROLLER_BINDTYPE_HAT:
    {
        int hm = b.value.hat.hat_mask;
        if (hm != 1 && hm != 2 && hm != 4 && hm != 8) return -1;
        return 0x100 | ((b.value.hat.hat & 0xF) << 4) | hm;
    }
    case SDL_CONTROLLER_BINDTYPE_AXIS:
        return 0xFFFF | 0x10000 | (axisdir << 20) | ((b.value.axis & 0xF) << 24);
    default:
        return -1;
    }
}

bool EmuInstance::fillMappingFromController(int devIndex, int* map)
{
    if (!SDL_IsGameController(devIndex)) return false;
    SDL_GameController* gc = SDL_GameControllerOpen(devIndex);
    if (!gc) return false;

    auto btn = [gc](SDL_GameControllerButton b, int axisdir = 0)
    {
        return bindToMapping(SDL_GameControllerGetBindForButton(gc, b), axisdir);
    };
    auto axis = [gc](SDL_GameControllerAxis a, int axisdir)
    {
        return bindToMapping(SDL_GameControllerGetBindForAxis(gc, a), axisdir);
    };

    for (int i = 0; i < numButtons; i++) map[i] = -1;

    // indices as in buttonNames; face buttons by position (Nintendo layout)
    map[0]  = btn(SDL_CONTROLLER_BUTTON_B);             // A     <- right face button
    map[1]  = btn(SDL_CONTROLLER_BUTTON_A);             // B     <- bottom
    map[2]  = btn(SDL_CONTROLLER_BUTTON_BACK);          // Select
    map[3]  = btn(SDL_CONTROLLER_BUTTON_START);         // Start
    map[4]  = btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    map[5]  = btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 1);
    map[6]  = btn(SDL_CONTROLLER_BUTTON_DPAD_UP, 1);
    map[7]  = btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    map[8]  = btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); // R
    map[9]  = btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);  // L
    map[10] = btn(SDL_CONTROLLER_BUTTON_Y);             // X     <- top
    map[11] = btn(SDL_CONTROLLER_BUTTON_X);             // Y     <- left
    map[12] = axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 2); // L2
    map[13] = axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 2);// R2
    map[14] = btn(SDL_CONTROLLER_BUTTON_LEFTSTICK);     // L3
    map[15] = btn(SDL_CONTROLLER_BUTTON_RIGHTSTICK);    // R3
    map[16] = axis(SDL_CONTROLLER_AXIS_LEFTX, 0);       // LS right
    map[17] = axis(SDL_CONTROLLER_AXIS_LEFTX, 1);       // LS left
    map[18] = axis(SDL_CONTROLLER_AXIS_LEFTY, 1);       // LS up
    map[19] = axis(SDL_CONTROLLER_AXIS_LEFTY, 0);       // LS down
    map[20] = axis(SDL_CONTROLLER_AXIS_RIGHTX, 0);
    map[21] = axis(SDL_CONTROLLER_AXIS_RIGHTX, 1);
    map[22] = axis(SDL_CONTROLLER_AXIS_RIGHTY, 1);
    map[23] = axis(SDL_CONTROLLER_AXIS_RIGHTY, 0);

    SDL_GameControllerClose(gc);

    for (int i = 0; i < numButtons; i++)
        if (map[i] != -1) return true;
    return false;
}

void EmuInstance::assignJoystick(int pad, int devIndex)
{
    if (pad == 0)
    {
        joystickID = devIndex;
        openJoystick();
    }
    else
    {
        extPads[pad-1].joystickID = devIndex;
        openExtJoystick(pad);
    }
    if (!getPadJoystick(pad)) return;

    Platform::Log(Platform::LogLevel::Info, "Joystick %d (%s) assigned to controller %d\n",
                  devIndex, SDL_JoystickNameForIndex(devIndex), pad + 1);

    autoFillPad(pad, devIndex, true);
}

void EmuInstance::autoFillPad(int pad, int devIndex, bool assigned)
{
    // fill the mappings only when this pad has no joystick mapping at all
    int* map = (pad == 0) ? joyMapping : extPads[pad-1].joyMapping;
    bool empty = true;
    for (int i = 0; i < numButtons; i++)
        if (map[i] != -1) { empty = false; break; }

    bool filled = false;
    if (empty)
    {
        int newmap[numButtons];
        if (fillMappingFromController(devIndex, newmap))
        {
            memcpy(map, newmap, sizeof(newmap));
            filled = true;
        }
    }

    if (filled || assigned)
        saveAutoMapping(pad, devIndex, filled);
}

void EmuInstance::saveAutoMapping(int pad, int joyIndex, bool mappingChanged)
{
    if (!mainWindow) return;

    std::vector<int> map;
    if (mappingChanged)
    {
        const int* src = getPadJoyMapping(pad);
        map.assign(src, src + numButtons);
    }

    // config + dialog live in the GUI thread
    QMetaObject::invokeMethod(mainWindow, [this, pad, joyIndex, map]()
    {
        Config::Table& instcfg = getLocalConfig();
        if (!map.empty())
        {
            if (pad == 0)
            {
                Config::Table joycfg = instcfg.GetTable("Joystick");
                for (int i = 0; i < numButtons; i++) joycfg.SetInt(buttonNames[i], map[i]);
            }
            else
            {
                Config::Table padcfg = instcfg.GetTable("Pad" + std::to_string(pad + 1));
                Config::Table joycfg = padcfg.GetTable("Joystick");
                for (int i = 0; i < numButtons; i++) joycfg.SetInt(buttonNames[i], map[i]);
            }
            Config::Save();
        }

        if (InputConfigDialog::currentDlg)
            InputConfigDialog::currentDlg->onJoystickAutoAssigned(pad, joyIndex, map.empty() ? nullptr : map.data());
    }, Qt::QueuedConnection);
}

void EmuInstance::inputHotplug()
{
    int num = SDL_NumJoysticks();
    std::vector<SDL_JoystickID> cur(num);
    for (int i = 0; i < num; i++)
        cur[i] = SDL_JoystickGetDeviceInstanceID(i);

    for (int i = 0; i < num; i++)
    {
        if (std::find(knownJoysticks.begin(), knownJoysticks.end(), cur[i]) != knownJoysticks.end())
            continue;
        // newly connected (or present at startup)
        if (isJoystickHeld(cur[i]))
        {
            // already opened from the config: still fill an all-None mapping
            for (int pn = 0; pn < numPads; pn++)
            {
                SDL_Joystick* j = getPadJoystick(pn);
                if (j && SDL_JoystickInstanceID(j) == cur[i])
                    autoFillPad(pn, i, false);
            }
            continue;
        }

        int pad = pickFreePad(SDL_JoystickGetDeviceGUID(i));
        if (pad < 0) continue;
        assignJoystick(pad, i);
    }

    knownJoysticks = cur;
}

void EmuInstance::openExtJoystick(int pad)
{
    ExtPad& p = extPads[pad-1];
    if (p.joystick) SDL_JoystickClose(p.joystick);
    p.joystick = nullptr;

    if (p.joystickID < 0 || p.joystickID >= SDL_NumJoysticks())
        return;

    p.joystick = SDL_JoystickOpen(p.joystickID);
}

void EmuInstance::openJoystick()
{
    if (controller) SDL_GameControllerClose(controller);
    controller = nullptr;
    hasRumble = false;

    if (joystick) SDL_JoystickClose(joystick);
    joystick = nullptr;

    int num = SDL_NumJoysticks();
    if (num < 1)
    {
        controller = nullptr;
        joystick = nullptr;
        hasRumble = false;
        hasAccelerometer = false;
        hasGyroscope = false;
        return;
    }

    if (joystickID >= num)
        joystickID = 0;
    if (joystickID < 0)
    {
        controller = nullptr;
        joystick = nullptr;
        hasRumble = false;
        hasAccelerometer = false;
        hasGyroscope = false;
        return;
    }

    joystick = SDL_JoystickOpen(joystickID);

    if (SDL_IsGameController(joystickID))
    {
        controller = SDL_GameControllerOpen(joystickID);
    }

    if (controller)
    {
        if (SDL_GameControllerHasRumble(controller))
        {
            hasRumble = true;
        }
        if (SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL))
        {
            hasAccelerometer = SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_ACCEL, SDL_TRUE) == 0;
        }
        if (SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO))
        {
            hasGyroscope = SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO, SDL_TRUE) == 0;
        }
    }
}

void EmuInstance::closeJoystick()
{
    if (controller)
    {
        SDL_GameControllerClose(controller);
        controller = nullptr;
        hasRumble = false;
        hasAccelerometer = false;
        hasGyroscope = false;
    }
    if (joystick)
    {
        SDL_JoystickClose(joystick);
        joystick = nullptr;
    }
}


// distinguish between left and right modifier keys (Ctrl, Alt, Shift)
// Qt provides no real cross-platform way to do this, so here we go
// for Windows and Linux we can distinguish via scancodes (but both
// provide different scancodes)
bool isRightModKey(QKeyEvent* event)
{
#ifdef __WIN32__
    quint32 scan = event->nativeScanCode();
    return (scan == 0x11D || scan == 0x138 || scan == 0x36);
#elif __APPLE__
    quint32 scan = event->nativeVirtualKey();
    return (scan == 0x36 || scan == 0x3C || scan == 0x3D || scan == 0x3E);
#else
    quint32 scan = event->nativeScanCode();
    return (scan == 0x69 || scan == 0x6C || scan == 0x3E);
#endif
}

int getEventKeyVal(QKeyEvent* event)
{
    int key = event->key();
    int mod = event->modifiers();
    bool ismod = (key == Qt::Key_Control ||
                  key == Qt::Key_Alt ||
                  key == Qt::Key_AltGr ||
                  key == Qt::Key_Shift ||
                  key == Qt::Key_Meta);

    if (!ismod)
        key |= mod;
    else if (isRightModKey(event))
        key |= (1<<31);

    return key;
}


void EmuInstance::onKeyPress(QKeyEvent* event)
{
    int keyHK = getEventKeyVal(event);
    int keyKP = keyHK;
    if (event->modifiers() != Qt::KeypadModifier)
        keyKP &= ~event->modifiers();

    for (int i = 0; i < numButtons; i++)
        if (keyKP == keyMapping[i])
            keyInputMask &= ~(1<<i);

    for (auto& p : extPads)
        for (int i = 0; i < numButtons; i++)
            if (keyKP == p.keyMapping[i])
                p.keyInputMask &= ~(1<<i);

    for (int i = 0; i < HK_MAX; i++)
        if (keyHK == hkKeyMapping[i])
            keyHotkeyMask |= (1<<i);
}

void EmuInstance::onKeyRelease(QKeyEvent* event)
{
    int keyHK = getEventKeyVal(event);
    int keyKP = keyHK;
    if (event->modifiers() != Qt::KeypadModifier)
        keyKP &= ~event->modifiers();

    for (int i = 0; i < numButtons; i++)
        if (keyKP == keyMapping[i])
            keyInputMask |= (1<<i);

    for (auto& p : extPads)
        for (int i = 0; i < numButtons; i++)
            if (keyKP == p.keyMapping[i])
                p.keyInputMask |= (1<<i);

    for (int i = 0; i < HK_MAX; i++)
        if (keyHK == hkKeyMapping[i])
            keyHotkeyMask &= ~(1<<i);
}

void EmuInstance::keyReleaseAll()
{
    keyInputMask = kAllReleased;
    keyHotkeyMask = 0;
    for (auto& p : extPads)
        p.keyInputMask = kAllReleased;
}

bool EmuInstance::joystickButtonDown(int val)
{
    return joystickButtonDown(joystick, val);
}

int EmuInstance::joystickAxisValue(SDL_Joystick* joy, int val, bool trigger)
{
    // analog value of an axis mapping (0..32767 towards the mapped direction),
    // -1 if the mapping has no axis
    if (!joy || val == -1 || !(val & 0x10000)) return -1;

    int axisnum = (val >> 24) & 0xF;
    int axisdir = (val >> 20) & 0xF;
    int v = SDL_JoystickGetAxis(joy, axisnum);

    switch (axisdir)
    {
        case 0: return std::clamp(v, 0, 32767);
        case 1: return std::clamp(-v, 0, 32767);
        case 2: return std::clamp((v + 32768) >> 1, 0, 32767);
    }
    return -1;
}

void EmuInstance::computePadAnalog(SDL_Joystick* joy, const int* joymap, melonDS::u32 mask, melonDS::s8* axes, melonDS::u8* trig)
{
    // button index of the + and - direction of each axis
    static const int axisDirs[4][2] =
    {
        {16, 17}, // LX: right, left
        {19, 18}, // LY: down, up
        {20, 21}, // RX
        {23, 22}, // RY
    };

    for (int a = 0; a < 4; a++)
    {
        int bp = axisDirs[a][0], bn = axisDirs[a][1];
        int vp = joystickAxisValue(joy, joymap[bp], false);
        int vn = joystickAxisValue(joy, joymap[bn], false);
        int v = 0;
        if (vp >= 0 || vn >= 0)
            v = std::max(vp, 0) - std::max(vn, 0);

        // digital input (keyboard, buttons, hats) gives full deflection
        bool dp = !(mask & (1 << bp)), dn = !(mask & (1 << bn));
        if (dp && !dn && v < 16384) v = 32767;
        if (dn && !dp && v > -16384) v = -32767;

        axes[a] = (melonDS::s8)std::clamp(v >> 8, -128, 127);
    }

    for (int t = 0; t < 2; t++)
    {
        int b = 12 + t; // L2, R2
        int v = joystickAxisValue(joy, joymap[b], true);
        int out = (v >= 0) ? (v >> 7) : 0;
        // pressed but not (far enough) by the axis itself: key or button press -> full
        if (!(mask & (1 << b)) && out <= 128) out = 255;
        trig[t] = (melonDS::u8)std::clamp(out, 0, 255);
    }
}

bool EmuInstance::joystickButtonDown(SDL_Joystick* joystick, int val)
{
    if (val == -1) return false;
    if (!joystick) return false;

    bool hasbtn = ((val & 0xFFFF) != 0xFFFF);

    if (hasbtn)
    {
        if (val & 0x100)
        {
            int hatnum = (val >> 4) & 0xF;
            int hatdir = val & 0xF;
            Uint8 hatval = SDL_JoystickGetHat(joystick, hatnum);

            bool pressed = false;
            if      (hatdir == 0x1) pressed = (hatval & SDL_HAT_UP);
            else if (hatdir == 0x4) pressed = (hatval & SDL_HAT_DOWN);
            else if (hatdir == 0x2) pressed = (hatval & SDL_HAT_RIGHT);
            else if (hatdir == 0x8) pressed = (hatval & SDL_HAT_LEFT);

            if (pressed) return true;
        }
        else
        {
            int btnnum = val & 0xFFFF;
            Uint8 btnval = SDL_JoystickGetButton(joystick, btnnum);

            if (btnval) return true;
        }
    }

    if (val & 0x10000)
    {
        int axisnum = (val >> 24) & 0xF;
        int axisdir = (val >> 20) & 0xF;
        Sint16 axisval = SDL_JoystickGetAxis(joystick, axisnum);

        switch (axisdir)
        {
            case 0: // positive
                if (axisval > 16384) return true;
                break;

            case 1: // negative
                if (axisval < -16384) return true;
                break;

            case 2: // trigger
                if (axisval > 0) return true;
                break;
        }
    }

    return false;
}

void EmuInstance::inputProcess()
{
    SDL_LockMutex(joyMutex.get());
    SDL_JoystickUpdate();

    if (joystick)
    {
        if (!SDL_JoystickGetAttached(joystick))
        {
            padLastGUID[0] = SDL_JoystickGetGUID(joystick);
            padHasGUID[0] = true;
            closeJoystick();
        }
    }
    for (int pn = 1; pn < numPads; pn++)
    {
        ExtPad& p = extPads[pn-1];
        if (p.joystick && !SDL_JoystickGetAttached(p.joystick))
        {
            padLastGUID[pn] = SDL_JoystickGetGUID(p.joystick);
            padHasGUID[pn] = true;
            SDL_JoystickClose(p.joystick);
            p.joystick = nullptr;
        }
    }

    inputHotplug();

    joyInputMask = kAllReleased;
    if (joystick)
    {
        for (int i = 0; i < numButtons; i++)
            if (joystickButtonDown(joyMapping[i]))
                joyInputMask &= ~(1 << i);
    }

    inputMask = keyInputMask & joyInputMask;
    computePadAnalog(joystick, joyMapping, inputMask, padAxes, padTriggers);

    for (int pn = 1; pn < numPads; pn++)
    {
        ExtPad& p = extPads[pn-1];

        melonDS::u32 jmask = kAllReleased;
        if (p.joystick)
        {
            for (int i = 0; i < numButtons; i++)
                if (joystickButtonDown(p.joystick, p.joyMapping[i]))
                    jmask &= ~(1 << i);
        }
        p.inputMask = p.keyInputMask & jmask;

        bool haskeys = false;
        for (int i = 0; i < numButtons; i++)
            if (p.keyMapping[i] != -1) { haskeys = true; break; }
        p.connected = multiController && ((p.joystick != nullptr) || haskeys);

        computePadAnalog(p.joystick, p.joyMapping, p.inputMask, p.axes, p.triggers);
    }

    joyHotkeyMask = 0;
    if (joystick)
    {
        for (int i = 0; i < HK_MAX; i++)
            if (joystickButtonDown(hkJoyMapping[i]))
                joyHotkeyMask |= (1 << i);
    }

    hotkeyMask = keyHotkeyMask | joyHotkeyMask;
    hotkeyPress = hotkeyMask & ~lastHotkeyMask;
    hotkeyRelease = lastHotkeyMask & ~hotkeyMask;
    lastHotkeyMask = hotkeyMask;
    SDL_UnlockMutex(joyMutex.get());
}

void EmuInstance::touchScreen(int x, int y)
{
    touchX = x;
    touchY = y;
    isTouching = true;
}

void EmuInstance::releaseScreen()
{
    isTouching = false;
}
