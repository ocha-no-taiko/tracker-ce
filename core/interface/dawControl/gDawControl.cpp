#include "dawControl/dawControl.h"

#include <stdio.h>
#include <string.h>

static const char* const kStateNames[dawStateCount] = {
    "PLAY", "REC", "S.REC", "OVERDUB", "METRO",
};

static constexpr const char* kNoteNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

void cDawControl::initDisplayControls()
{
    strControlProperties prop;

    // bottom screen-button labels
    for (uint8_t i = 0; i < 8; i++)
    {
        prop.colors = display.getTheme().activeLabelsColors;
        prop.text = (char*)"";
        prop.style = (controlStyleShow | controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        prop.x = (800 / 8) * i + (800 / 16);
        prop.y = 424;
        prop.w = 800 / 8 - 6;
        prop.h = 55;
        prop.value = 0;
        label[i] = display.createControl<cLabel>(&prop);
    }

    strControlProperties bg;
    bg.text = nullptr;
    bg.colors = nullptr;
    bg.value = 255;
    bg.style = controlStyleNoTransparency | controlStyleShow;
    bg.x = 0;
    bg.w = 800;
    bg.y = 425;
    bg.h = 54;
    bgLabel = display.createControl<cBgLabel>(&bg);

    // transport / recording states
    for (uint8_t i = 0; i < dawStateCount; i++)
    {
        prop.colors = display.getTheme().inactiveButtonLabelsColors;
        prop.text = kStateNames[i];
        prop.style = (controlStyleShow | controlStyleCenterX | controlStyleCenterY | controlStyleFont4 |
                      controlStyleBorder | controlStyleRoundedBorder);
        prop.x = (800 / dawStateCount) * i + (800 / dawStateCount / 2);
        prop.y = 95;
        prop.w = 800 / dawStateCount - 20;
        prop.h = 60;
        stateLabel[i] = display.createControl<cLabel>(&prop);
    }

    // info lines: tempo, session ring, mode, connection
    static constexpr uint16_t kInfoY[4] = { 190, 250, 300, 370 };
    static constexpr uint32_t kInfoFont[4] = { controlStyleFont4, controlStyleFont2, controlStyleFont2,
                                               controlStyleFont1 };
    for (uint8_t i = 0; i < 4; i++)
    {
        prop.colors = display.getTheme().activeLabelsColors;
        prop.text = "";
        prop.style = (controlStyleShow | controlStyleCenterX | controlStyleCenterY | kInfoFont[i]);
        prop.x = 400;
        prop.y = kInfoY[i];
        prop.w = 780;
        prop.h = 40;
        infoLabel[i] = display.createControl<cLabel>(&prop);
    }
}

void cDawControl::showDefaultScreen()
{
    snprintf(titleText, sizeof(titleText), "DAW Control");
    titleBar->show();
    titleBar->setTitle(titleText);

    refreshBottomLabels();
    refreshStates();
    refreshInfo();

    display.refreshControl(bgLabel);
    display.synchronizeRefresh();
}

void cDawControl::refreshBottomLabels()
{
    const DawControlConfig& cfg = dawControlConfig;
    snprintf(modeButtonText, sizeof(modeButtonText), "%s", sessionMode ? "> Note" : "> Session");

    for (uint8_t i = 0; i < 8; i++)
    {
        const char* text = cfg.labels[i];
        if (i == cfg.modeButton && text[0] == '\0')
            text = modeButtonText;
        display.setControlText(label[i], text);
        display.refreshControl(label[i]);
    }
}

void cDawControl::refreshStates()
{
    const Theme& theme = display.getTheme();
    for (uint8_t i = 0; i < dawStateCount; i++)
    {
        const uint32_t* colors = theme.inactiveButtonLabelsColors;
        if (states[i])
        {
            bool isRecord = (i == dawStateArrangementRecord || i == dawStateSessionRecord);
            colors = isRecord ? theme.redButtonLabelsColors : theme.activeButtonLabelsColors;
        }
        display.setControlColors(stateLabel[i], colors);
        display.refreshControl(stateLabel[i]);
    }
}

void cDawControl::refreshInfo()
{
    const DawControlConfig& cfg = dawControlConfig;

    snprintf(tempoText, sizeof(tempoText), "%u.%u BPM", tempoTenths / 10, tempoTenths % 10);

    snprintf(ringText,
             sizeof(ringText),
             "Tracks %u-%u   Scenes %u-%u   Selected track %u",
             ringTrack + 1,
             ringTrack + kPadColumns,
             ringScene + 1,
             ringScene + kPadRows,
             selectedTrack + 1);

    if (sessionMode)
    {
        snprintf(modeText, sizeof(modeText), "Pads: Session (clip launch)");
    }
    else
    {
        int16_t low = cfg.noteRoot + octaveShift * 12;
        if (low < 0)
            low = 0;
        snprintf(modeText,
                 sizeof(modeText),
                 "Pads: Note  %s%d and up  (MIDI ch %u)",
                 kNoteNames[low % 12],
                 low / 12 - 2,
                 cfg.noteChannel);
    }

    snprintf(statusText,
             sizeof(statusText),
             "%s   |   USB ch %u   |   %s",
             connected ? "Ableton connected" : "Waiting for Ableton...",
             cfg.controlChannel,
             cfg.loadedFromSd ? "DawControl.json loaded" : "Default settings");

    display.setControlText(infoLabel[0], tempoText);
    display.setControlText(infoLabel[1], ringText);
    display.setControlText(infoLabel[2], modeText);
    display.setControlText(infoLabel[3], statusText);
    for (uint8_t i = 0; i < 4; i++)
        display.refreshControl(infoLabel[i]);
}
