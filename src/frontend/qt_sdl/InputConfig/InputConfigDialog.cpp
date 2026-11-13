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

#include <QGroupBox>
#include <QLabel>
#include <QKeyEvent>
#include <QDebug>
#include <QTabWidget>
#include <QTabBar>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QSignalBlocker>
#include <cstring>

#include <SDL2/SDL.h>

#include <string>

#include "types.h"
#include "Platform.h"

#include "InputConfigDialog.h"
#include "ui_InputConfigDialog.h"
#include "MapButton.h"


using namespace melonDS;
InputConfigDialog* InputConfigDialog::currentDlg = nullptr;

// indices into EmuInstance::buttonNames
enum
{
    B_A = 0, B_B, B_Select, B_Start, B_Right, B_Left, B_Up, B_Down, B_R, B_L, B_X, B_Y,
    B_L2, B_R2, B_L3, B_R3,
    B_LSRight, B_LSLeft, B_LSUp, B_LSDown,
    B_RSRight, B_RSLeft, B_RSUp, B_RSDown,
};

static std::string padTableName(int pad)
{
    return "Pad" + std::to_string(pad + 1);
}

InputConfigDialog::InputConfigDialog(QWidget* parent) : QDialog(parent), ui(new Ui::InputConfigDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    emuInstance = ((MainWindow*)parent)->getEmuInstance();

    Config::Table& instcfg = emuInstance->getLocalConfig();
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    Config::Table joycfg = instcfg.GetTable("Joystick");

    for (int p = 0; p < pad_num; p++)
    {
        if (p > 0)
        {
            Config::Table padcfg = instcfg.GetTable(padTableName(p));
            loadPadMappings(p, padcfg.GetTable("Keyboard"), padcfg.GetTable("Joystick"));
        }
        else
            loadPadMappings(p, keycfg, joycfg);

        // the joystick this pad really uses right now (may have been hotplug-assigned)
        padJoystickID[p] = emuInstance->getPadJoystickIndex(p);
        padJoystickOrig[p] = padJoystickID[p];

        // keep live auto-filled mappings that may not have reached the config yet
        const int* live = emuInstance->getPadJoyMapping(p);
        bool cfgEmpty = true;
        for (int i = 0; i < keypad_num; i++)
            if (keypadJoyMap[p][i] != -1) { cfgEmpty = false; break; }
        if (cfgEmpty)
            for (int i = 0; i < keypad_num; i++) keypadJoyMap[p][i] = live[i];
    }
    joystickID = padJoystickID[0];

    int i = 0;
    for (int hotkey : hk_addons)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        addonsKeyMap[i] = keycfg.GetInt(btn);
        addonsJoyMap[i] = joycfg.GetInt(btn);
        i++;
    }

    i = 0;
    for (int hotkey : hk_general)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        hkGeneralKeyMap[i] = keycfg.GetInt(btn);
        hkGeneralJoyMap[i] = joycfg.GetInt(btn);
        i++;
    }

    populatePage(ui->tabAddons, hk_addons_labels, addonsKeyMap, addonsJoyMap);
    populatePage(ui->tabHotkeysGeneral, hk_general_labels, hkGeneralKeyMap, hkGeneralJoyMap);

    setupKeypadPage();

    int inst = emuInstance->getInstanceID();
    if (inst > 0)
        ui->lblInstanceNum->setText(QString("Configuring mappings for instance %1").arg(inst+1));
    else
        ui->lblInstanceNum->hide();
}

void InputConfigDialog::loadPadMappings(int pad, Config::Table keycfg, Config::Table joycfg)
{
    for (int i = 0; i < keypad_num; i++)
    {
        const char* btn = EmuInstance::buttonNames[i];
        keypadKeyMap[pad][i] = keycfg.GetInt(btn);
        keypadJoyMap[pad][i] = joycfg.GetInt(btn);
    }
}

void InputConfigDialog::savePadMappings(int pad, Config::Table keycfg, Config::Table joycfg)
{
    for (int i = 0; i < keypad_num; i++)
    {
        const char* btn = EmuInstance::buttonNames[i];
        keycfg.SetInt(btn, keypadKeyMap[pad][i]);
        joycfg.SetInt(btn, keypadJoyMap[pad][i]);
    }
}

InputConfigDialog::~InputConfigDialog()
{
    delete ui;
}

QWidget* InputConfigDialog::makeMapItem(int pad, bool joy, int btn, const char* label)
{
    QGroupBox* grp = new QGroupBox();
    QVBoxLayout* lay = new QVBoxLayout();
    lay->setContentsMargins(6, 4, 6, 6);
    lay->setSpacing(3);

    QLabel* lbl = new QLabel(QString("<b>%1</b>").arg(label));
    lbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(lbl);

    QPushButton* b;
    if (joy)
    {
        JoyMapButton* jb = new JoyMapButton(&keypadJoyMap[pad][btn], false, pad);
        padJoyButtons[pad].append(jb);
        b = jb;
    }
    else
    {
        KeyMapButton* kb = new KeyMapButton(&keypadKeyMap[pad][btn], false);
        padKeyButtons[pad].append(kb);
        b = kb;
    }
    b->setMinimumWidth(108);
    lay->addWidget(b);

    grp->setLayout(lay);
    grp->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return grp;
}

QWidget* InputConfigDialog::makeCluster(int pad, bool joy, int up, int left, int mid, int right, int down,
                                        const char* lu, const char* ll, const char* lm, const char* lr, const char* ld)
{
    // plus-shaped group: up on top, left/(mid)/right in the middle, down at the bottom
    QGroupBox* cl = new QGroupBox();
    cl->setObjectName("cluster");
    QGridLayout* g = new QGridLayout();
    g->setContentsMargins(0, 0, 0, 0);
    g->setSpacing(2);

    g->addWidget(makeMapItem(pad, joy, up, lu), 0, 1, Qt::AlignCenter);
    g->addWidget(makeMapItem(pad, joy, left, ll), 1, 0, Qt::AlignCenter);
    if (mid >= 0)
        g->addWidget(makeMapItem(pad, joy, mid, lm), 1, 1, Qt::AlignCenter);
    g->addWidget(makeMapItem(pad, joy, right, lr), 1, 2, Qt::AlignCenter);
    g->addWidget(makeMapItem(pad, joy, down, ld), 2, 1, Qt::AlignCenter);

    cl->setLayout(g);
    return cl;
}

QWidget* InputConfigDialog::buildMappingGrid(int pad, bool joy)
{
    QWidget* page = new QWidget();
    QGridLayout* g = new QGridLayout();
    g->setVerticalSpacing(6);
    g->setHorizontalSpacing(10);

    QLabel* title = new QLabel(joy ? "Joystick mappings" : "Keyboard mappings");
    QFont f = title->font();
    f.setPointSize(14);
    title->setFont(f);
    title->setAlignment(Qt::AlignCenter);
    g->addWidget(title, 0, 1);

    // shoulders: L2/R2 above L/R, beside the back view of the DS
    QVBoxLayout* lcol = new QVBoxLayout();
    lcol->setSpacing(4);
    QWidget* w;
    lcol->addWidget(w = makeMapItem(pad, joy, B_L2, "L2"), 0, Qt::AlignRight);
    extWidgets.append(w);
    lcol->addWidget(makeMapItem(pad, joy, B_L, "L"), 0, Qt::AlignRight);
    g->addLayout(lcol, 1, 0, Qt::AlignRight | Qt::AlignVCenter);

    QVBoxLayout* rcol = new QVBoxLayout();
    rcol->setSpacing(4);
    rcol->addWidget(w = makeMapItem(pad, joy, B_R2, "R2"), 0, Qt::AlignLeft);
    extWidgets.append(w);
    rcol->addWidget(makeMapItem(pad, joy, B_R, "R"), 0, Qt::AlignLeft);
    g->addLayout(rcol, 1, 2, Qt::AlignLeft | Qt::AlignVCenter);

    QLabel* back = new QLabel();
    back->setPixmap(QPixmap(":/ds/ds_back.svg"));
    back->setAlignment(Qt::AlignCenter);
    g->addWidget(back, 1, 1);

    QLabel* open = new QLabel();
    open->setPixmap(QPixmap(":/ds/ds_open.svg"));
    open->setAlignment(Qt::AlignCenter);
    g->addWidget(open, 2, 1, 2, 1);

    // d-pad, then the left stick (L3 in the middle) below it
    g->addWidget(makeCluster(pad, joy, B_Up, B_Left, -1, B_Right, B_Down,
                             "Up", "Left", nullptr, "Right", "Down"),
                 2, 0, Qt::AlignRight | Qt::AlignVCenter);
    g->addWidget(w = makeCluster(pad, joy, B_LSUp, B_LSLeft, B_L3, B_LSRight, B_LSDown,
                                 "LS Up", "LS Left", "L3", "LS Right", "LS Down"),
                 3, 0, Qt::AlignRight | Qt::AlignVCenter);
    extWidgets.append(w);

    // face buttons, then the right stick (R3 in the middle) below them
    g->addWidget(makeCluster(pad, joy, B_X, B_Y, -1, B_A, B_B,
                             "X", "Y", nullptr, "A", "B"),
                 2, 2, Qt::AlignLeft | Qt::AlignVCenter);
    g->addWidget(w = makeCluster(pad, joy, B_RSUp, B_RSLeft, B_R3, B_RSRight, B_RSDown,
                                 "RS Up", "RS Left", "R3", "RS Right", "RS Down"),
                 3, 2, Qt::AlignLeft | Qt::AlignVCenter);
    extWidgets.append(w);

    QHBoxLayout* ss = new QHBoxLayout();
    ss->addStretch(1);
    ss->addWidget(makeMapItem(pad, joy, B_Select, "Select"));
    ss->addSpacing(50);
    ss->addWidget(makeMapItem(pad, joy, B_Start, "Start"));
    ss->addStretch(1);
    g->addLayout(ss, 4, 1);

    QHBoxLayout* sw = new QHBoxLayout();
    sw->addStretch(1);
    QPushButton* swbtn = new QPushButton(joy ? "Switch to Keyboard mappings" : "Switch to Joystick mappings");
    sw->addWidget(swbtn);
    sw->addStretch(1);
    g->addLayout(sw, 5, 0, 1, 3);

    connect(swbtn, &QPushButton::clicked, this, [this, pad, joy]()
    {
        padStack[pad]->setCurrentIndex(joy ? 0 : 1);
    });

    g->setRowStretch(6, 1);
    page->setLayout(g);
    return page;
}

QWidget* InputConfigDialog::buildPadPage(int pad)
{
    QWidget* page = new QWidget();
    QVBoxLayout* v = new QVBoxLayout();

    QStackedWidget* stack = new QStackedWidget();
    stack->addWidget(buildMappingGrid(pad, false));
    stack->addWidget(buildMappingGrid(pad, true));
    padStack[pad] = stack;
    v->addWidget(stack);

    QHBoxLayout* jl = new QHBoxLayout();
    jl->addWidget(new QLabel(QString("Joystick for controller %1:").arg(pad + 1)));
    QComboBox* cbx = new QComboBox();
    cbx->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    padJoyCombo[pad] = cbx;
    jl->addWidget(cbx, 1);
    v->addLayout(jl);

    QPushButton* autobtn = new QPushButton("Auto-fill");
    autobtn->setToolTip("Fill this controller's joystick mappings from the selected joystick (SDL controller database)");
    jl->addWidget(autobtn);

    QPushButton* clearbtn = new QPushButton("Unbind all");
    clearbtn->setToolTip("Set every mapping on the page shown (keyboard or joystick) of this controller to None");
    jl->addWidget(clearbtn);

    fillJoyCombo(pad);

    connect(cbx, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, pad](int idx)
    {
        if (idx < 0) return;
        int id = padJoyCombo[pad]->itemData(idx).toInt();
        if (id == -2) return; // "(no joysticks available)"
        padJoyChanged[pad] = true;
        // may take the joystick away from another controller
        emuInstance->setPadJoystick(pad, id);
        refreshJoySelection();
    });

    connect(clearbtn, &QPushButton::clicked, this, [this, pad]()
    {
        bool joy = padStack[pad]->currentIndex() == 1;
        for (int i = 0; i < keypad_num; i++)
        {
            if (joy) keypadJoyMap[pad][i] = -1;
            else     keypadKeyMap[pad][i] = -1;
        }
        if (joy) for (JoyMapButton* jb : padJoyButtons[pad]) jb->refresh();
        else     for (KeyMapButton* kb : padKeyButtons[pad]) kb->refresh();
    });

    connect(autobtn, &QPushButton::clicked, this, [this, pad]()
    {
        int id = padJoystickID[pad];
        if (id < 0 || id >= SDL_NumJoysticks())
        {
            QMessageBox::information(this, "melonDS", "Select a joystick for this controller first.");
            return;
        }
        int map[keypad_num];
        auto mutex = emuInstance->getJoyMutex();
        SDL_LockMutex(mutex.get());
        bool ok = EmuInstance::autoMapController(id, map);
        SDL_UnlockMutex(mutex.get());
        if (!ok)
        {
            QMessageBox::information(this, "melonDS",
                "This joystick is not in SDL's controller database, so its buttons can't be filled in automatically.");
            return;
        }
        memcpy(keypadJoyMap[pad], map, sizeof(map));
        for (JoyMapButton* jb : padJoyButtons[pad]) jb->refresh();
        padStack[pad]->setCurrentIndex(1);
    });

    int njoy = SDL_NumJoysticks();
    // show the joystick page when this controller has a joystick
    bool hasjoy = (padJoystickID[pad] >= 0) && (padJoystickID[pad] < njoy);
    stack->setCurrentIndex(hasjoy ? 1 : 0);

    page->setLayout(v);
    return page;
}

void InputConfigDialog::fillJoyCombo(int pad)
{
    QComboBox* cbx = padJoyCombo[pad];
    QSignalBlocker block(cbx);
    cbx->clear();

    int njoy = SDL_NumJoysticks();
    if (njoy < 1 && pad == 0)
    {
        cbx->addItem("(no joysticks available)", -2);
        cbx->setEnabled(false);
        return;
    }
    cbx->setEnabled(true);
    cbx->addItem("(none)", -1);
    for (int i = 0; i < njoy; i++)
        cbx->addItem(QString("%1: %2").arg(i + 1).arg(SDL_JoystickNameForIndex(i)), i);

    int id = padJoystickID[pad];
    if (id >= njoy) id = -1;
    cbx->setCurrentIndex(id + 1);
}

void InputConfigDialog::refreshJoySelection()
{
    for (int p = 0; p < pad_num; p++)
    {
        int id = emuInstance->getPadJoystickIndex(p);
        if (id != padJoystickID[p]) padJoyChanged[p] = true;
        padJoystickID[p] = id;
        fillJoyCombo(p);
    }
    joystickID = padJoystickID[0];
}

void InputConfigDialog::onJoystickAutoAssigned(int pad, int joyIndex, const int* mapping)
{
    padJoystickID[pad] = joyIndex;
    padJoystickOrig[pad] = joyIndex;
    if (pad == 0) joystickID = joyIndex;
    for (int p = 0; p < pad_num; p++) fillJoyCombo(p);

    if (mapping)
    {
        memcpy(keypadJoyMap[pad], mapping, sizeof(keypadJoyMap[pad]));
        for (JoyMapButton* jb : padJoyButtons[pad]) jb->refresh();
        padStack[pad]->setCurrentIndex(1);
    }
}

void InputConfigDialog::setupKeypadPage()
{
    Config::Table& instcfg = emuInstance->getLocalConfig();

    QHBoxLayout* toggles = new QHBoxLayout();
    chkExtended = new QCheckBox("Extended buttons (L2/R2, L3/R3, analog sticks)");
    chkExtended->setToolTip("Reports the extra inputs in the reserved KEYINPUT/EXTKEYIN bits and in the multi-pad block at 0x04FFF000");
    chkExtended->setChecked(instcfg.GetBool("ExtendedButtons"));
    chkMulti = new QCheckBox("Multiple controllers (2-8)");
    chkMulti->setToolTip("Reports controllers 2-8 in the multi-pad block at 0x04FFF000");
    chkMulti->setChecked(instcfg.GetBool("MultiController"));
    toggles->addWidget(chkExtended);
    toggles->addSpacing(20);
    toggles->addWidget(chkMulti);
    toggles->addStretch(1);

    padTabs = new QTabWidget();
    padTabs->setTabPosition(QTabWidget::West);

    for (int p = 0; p < pad_num; p++)
        padTabs->addTab(buildPadPage(p), QString("Controller %1").arg(p + 1));

    QVBoxLayout* v = new QVBoxLayout();
    v->setContentsMargins(4, 4, 4, 4);
    v->addLayout(toggles);
    v->addWidget(padTabs);
    ui->tabInput->setLayout(v);

    connect(chkExtended, &QCheckBox::toggled, this, [this]() { applyToggles(); });
    connect(chkMulti, &QCheckBox::toggled, this, [this]() { applyToggles(); });
    applyToggles();
}

void InputConfigDialog::applyToggles()
{
    bool ext = chkExtended->isChecked();
    bool multi = chkMulti->isChecked();

    for (QWidget* w : extWidgets)
        w->setVisible(ext);

    if (!multi) padTabs->setCurrentIndex(0);
    for (int p = 1; p < pad_num; p++)
        padTabs->setTabVisible(p, multi);
    // a single controller doesn't need the side tabs at all
    padTabs->tabBar()->setVisible(multi);
}

void InputConfigDialog::populatePage(QWidget* page,
    const std::initializer_list<const char*>& labels,
    int* keymap, int* joymap)
{
    // kind of a hack
    bool ishotkey = (page != ui->tabInput);

    QHBoxLayout* main_layout = new QHBoxLayout();

    QGroupBox* group;
    QGridLayout* group_layout;

    group = new QGroupBox("Keyboard mappings:");
    main_layout->addWidget(group);
    group_layout = new QGridLayout();
    group_layout->setSpacing(1);
    int i = 0;
    for (const char* labelStr : labels)
    {
        QLabel* label = new QLabel(QString(labelStr)+":");
        KeyMapButton* btn = new KeyMapButton(&keymap[i], ishotkey);

        group_layout->addWidget(label, i, 0);
        group_layout->addWidget(btn, i, 1);
        i++;
    }
    group_layout->setRowStretch(labels.size(), 1);
    group->setLayout(group_layout);
    group->setMinimumWidth(275);

    group = new QGroupBox("Joystick mappings:");
    main_layout->addWidget(group);
    group_layout = new QGridLayout();
    group_layout->setSpacing(1);
    i = 0;
    for (const char* labelStr : labels)
    {
        QLabel* label = new QLabel(QString(labelStr)+":");
        JoyMapButton* btn = new JoyMapButton(&joymap[i], ishotkey);

        group_layout->addWidget(label, i, 0);
        group_layout->addWidget(btn, i, 1);
        i++;
    }
    group_layout->setRowStretch(labels.size(), 1);
    group->setLayout(group_layout);
    group->setMinimumWidth(275);

    page->setLayout(main_layout);
}

void InputConfigDialog::on_InputConfigDialog_accepted()
{
    Config::Table& instcfg = emuInstance->getLocalConfig();
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    Config::Table joycfg = instcfg.GetTable("Joystick");

    for (int p = 0; p < pad_num; p++)
    {
        if (p > 0)
        {
            Config::Table padcfg = instcfg.GetTable(padTableName(p));
            // keep the saved controller of an unplugged pad unless the user changed it
            if (padJoystickID[p] >= 0 || padJoyChanged[p])
            {
                padcfg.SetInt("JoystickID", padJoystickID[p]);
                // "(none)" chosen here: keep the controller empty, no auto-assign
                padcfg.SetString("JoystickGUID", padJoystickID[p] < 0 ? std::string("none") : emuInstance->getPadJoystickGUID(p));
            }
            savePadMappings(p, padcfg.GetTable("Keyboard"), padcfg.GetTable("Joystick"));
        }
        else
        {
            if (padJoystickID[0] >= 0 || padJoyChanged[0])
            {
                instcfg.SetInt("JoystickID", padJoystickID[0]);
                instcfg.SetString("JoystickGUID", padJoystickID[0] < 0 ? std::string("none") : emuInstance->getPadJoystickGUID(0));
            }
            savePadMappings(p, keycfg, joycfg);
        }
    }

    int i = 0;
    for (int hotkey : hk_addons)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        keycfg.SetInt(btn, addonsKeyMap[i]);
        joycfg.SetInt(btn, addonsJoyMap[i]);
        i++;
    }

    i = 0;
    for (int hotkey : hk_general)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        keycfg.SetInt(btn, hkGeneralKeyMap[i]);
        joycfg.SetInt(btn, hkGeneralJoyMap[i]);
        i++;
    }

    instcfg.SetBool("ExtendedButtons", chkExtended->isChecked());
    instcfg.SetBool("MultiController", chkMulti->isChecked());
    Config::Save();

    emuInstance->inputLoadConfig();

    closeDlg();
}

void InputConfigDialog::on_InputConfigDialog_rejected()
{
    // undo joystick selections made in this dialog
    for (int p = 0; p < pad_num; p++)
        if (padJoystickID[p] != padJoystickOrig[p])
            emuInstance->setPadJoystick(p, padJoystickOrig[p]);

    closeDlg();
}

SDL_Joystick* InputConfigDialog::getJoystick(int pad)
{
    return emuInstance->getPadJoystick(pad);
}

std::shared_ptr<SDL_mutex> InputConfigDialog::getJoyMutex()
{
    return emuInstance->getJoyMutex();
}
