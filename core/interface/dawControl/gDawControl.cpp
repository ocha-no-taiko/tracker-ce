#include "dawControl/dawControl.h"

#include <stdio.h>
#include <string.h>

#include "cr_section_macros.h"
#include "dawControl/dawScreen.h"

// What the screen shows; the display controls keep a pointer to it.
__NOINIT(EXTERNAL_RAM) static DawScreenData s_screen;

void cDawControl::initDisplayControls()
{
    s_screen.cfg = &dawControlConfig;
    s_screen.dawFont = false;
    memset(s_screen.info, 0, sizeof(s_screen.info));
    memset(s_screen.labels, 0, sizeof(s_screen.labels));
    dawScreenSetDefaultHelp(s_screen);

    // Controls are drawn in the reverse of the order they are created in,
    // so the background (created last) is drawn first.
    static constexpr DawScreenPart kOrder[4] = {
        dawScreenLabels, dawScreenHelp, dawScreenInfo, dawScreenBackground,
    };
    for (DawScreenPart part : kOrder)
    {
        strControlProperties prop;
        prop.text = nullptr;
        prop.colors = nullptr;
        prop.style = controlStyleShow;
        prop.x = 0;
        prop.y = 0;
        prop.w = 800;
        prop.h = 480;
        prop.value = part;
        prop.data = &s_screen;
        screenPart[part] = display.createControl<cDawScreen>(&prop);
    }
}

void cDawControl::showDefaultScreen()
{
    titleBar->hide();
    s_screen.dawFont = screenFont;

    refreshBottomLabels();
    refreshInfo();
    refreshHelp();

    display.refreshControl(screenPart[dawScreenBackground]);
    display.synchronizeRefresh();
}

void cDawControl::refreshBottomLabels()
{
    const DawControlConfig& cfg = dawControlConfig;

    for (uint8_t i = 0; i < 8; i++)
    {
        const char* text = cfg.labels[i];
        if (i == cfg.modeButton && text[0] == '\0')
            text = sessionMode ? "> Note" : "> Session";
        snprintf(s_screen.labels[i], sizeof(s_screen.labels[i]), "%s", text);
    }
    display.refreshControl(screenPart[dawScreenLabels]);
}

void cDawControl::refreshInfo()
{
    const DawControlConfig& cfg = dawControlConfig;
    auto& info = s_screen.info;

    if (!connected)
        snprintf(info[0], sizeof(info[0]), "%s", cfg.waitingText);
    else if (!namesReceived)
        info[0][0] = '\0';
    else
        snprintf(info[0], sizeof(info[0]), "%s", fileName[0] ? fileName : cfg.unsavedText);
    snprintf(info[1], sizeof(info[1]), "%s", namesReceived ? projectName : "");

    if (connected)
    {
        if (tempoTenths % 10 == 0)
            snprintf(info[2], sizeof(info[2]), "%u", tempoTenths / 10);
        else
            snprintf(info[2], sizeof(info[2]), "%u.%u", tempoTenths / 10, tempoTenths % 10);
        snprintf(info[3], sizeof(info[3]), "%u/%u", signatureNumerator, signatureDenominator);
        snprintf(info[4], sizeof(info[4]), "%s", states[dawStateMetronome] ? cfg.metroOn : cfg.metroOff);
    }
    else
    {
        info[2][0] = '\0';
        info[3][0] = '\0';
        info[4][0] = '\0';
    }

    display.refreshControl(screenPart[dawScreenInfo]);
}

void cDawControl::refreshHelp()
{
    display.refreshControl(screenPart[dawScreenHelp]);
}

void cDawControl::setHelpLine(uint8_t index, const char* text)
{
    dawScreenSetHelpLine(s_screen, index, text);
}

void cDawControl::resetHelp()
{
    dawScreenSetDefaultHelp(s_screen);
}
