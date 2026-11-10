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

#ifndef INPUTCONFIGDIALOG_H
#define INPUTCONFIGDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QComboBox>
#include <QStackedWidget>
#include <QCheckBox>
#include <QTabWidget>
#include <QList>
#include <initializer_list>

#include "Config.h"
#include "EmuInstance.h"

static constexpr int keypad_num = EmuInstance::numButtons;
static constexpr int pad_num = EmuInstance::numPads;

static constexpr std::initializer_list<int> hk_addons =
{
    HK_SolarSensorIncrease,
    HK_SolarSensorDecrease,
    HK_GuitarGripGreen,
    HK_GuitarGripRed,
    HK_GuitarGripYellow,
    HK_GuitarGripBlue,
};

static constexpr std::initializer_list<const char*> hk_addons_labels =
{
    "[Boktai] Sunlight + ",
    "[Boktai] Sunlight - ",
    "[Guitar Grip] Green",
    "[Guitar Grip] Red",
    "[Guitar Grip] Yellow",
    "[Guitar Grip] Blue",
};

static_assert(hk_addons.size() == hk_addons_labels.size());

static constexpr std::initializer_list<int> hk_general =
{
    HK_Pause,
    HK_Reset,
    HK_FrameStep,
    HK_FastForward,
    HK_FastForwardToggle,
    HK_SlowMo,
    HK_SlowMoToggle,
    HK_FrameLimitToggle,
    HK_FullscreenToggle,
    HK_Lid,
    HK_Mic,
    HK_SwapScreens,
    HK_SwapScreenEmphasis,
    HK_PowerButton,
    HK_VolumeUp,
    HK_VolumeDown,
    HK_AudioMuteToggle
};

static constexpr std::initializer_list<const char*> hk_general_labels =
{
    "Pause/resume",
    "Reset",
    "Frame step",
    "Fast forward",
    "Toggle fast forward",
    "Slow mo",
    "Toggle slow mo",
    "Toggle FPS limit",
    "Toggle fullscreen",
    "Close/open lid",
    "Microphone",
    "Swap screens",
    "Swap screen emphasis",
    "DSi Power button",
    "DSi Volume up",
    "DSi Volume down",
    "Toggle audio mute"
};

static_assert(hk_general.size() == hk_general_labels.size());


namespace Ui { class InputConfigDialog; }
class InputConfigDialog;

class InputConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InputConfigDialog(QWidget* parent);
    ~InputConfigDialog();

    SDL_Joystick* getJoystick(int pad = 0);
    // called (GUI thread) when a hotplugged joystick was given to a pad
    void onJoystickAutoAssigned(int pad, int joyIndex, const int* mapping);
    std::shared_ptr<SDL_mutex> getJoyMutex();

    static InputConfigDialog* currentDlg;
    static InputConfigDialog* openDlg(QWidget* parent)
    {
        if (currentDlg)
        {
            currentDlg->activateWindow();
            return currentDlg;
        }

        currentDlg = new InputConfigDialog(parent);
        currentDlg->open();
        return currentDlg;
    }
    static void closeDlg()
    {
        currentDlg = nullptr;
    }

private slots:
    void on_InputConfigDialog_accepted();
    void on_InputConfigDialog_rejected();


private:
    void populatePage(QWidget* page,
        const std::initializer_list<const char*>& labels,
        int* keymap, int* joymap);
    void setupKeypadPage();
    QWidget* buildPadPage(int pad);
    QWidget* buildMappingGrid(int pad, bool joy);
    QWidget* makeMapItem(int pad, bool joy, int btn, const char* label);
    QWidget* makeCluster(int pad, bool joy, int up, int left, int mid, int right, int down,
                         const char* lu, const char* ll, const char* lm, const char* lr, const char* ld);

    Ui::InputConfigDialog* ui;

    EmuInstance* emuInstance;

    int keypadKeyMap[pad_num][keypad_num], keypadJoyMap[pad_num][keypad_num];
    int padJoystickID[pad_num];
    int padJoystickOrig[pad_num];
    QCheckBox* chkExtended;
    QCheckBox* chkMulti;
    QTabWidget* padTabs;
    QList<QWidget*> extWidgets;
    void applyToggles();
    QList<class JoyMapButton*> padJoyButtons[pad_num];
    void loadPadMappings(int pad, Config::Table keycfg, Config::Table joycfg);
    void savePadMappings(int pad, Config::Table keycfg, Config::Table joycfg);
    void fillJoyCombo(int pad);
    QStackedWidget* padStack[pad_num];
    QComboBox* padJoyCombo[pad_num];
    int addonsKeyMap[hk_addons.size()], addonsJoyMap[hk_addons.size()];
    int hkGeneralKeyMap[hk_general.size()], hkGeneralJoyMap[hk_general.size()];
    int joystickID;
};


#endif // INPUTCONFIGDIALOG_H
