#include "q4/q4.h"

#include <string.h>

#include "cr_section_macros.h"
#include "q4/q4Screen.h"

// What the screen shows; the display controls keep a pointer to it.
__NOINIT(EXTERNAL_RAM) Q4ScreenData q4ScreenData;

void cQ4::initDisplayControls()
{
    q4ScreenData.cfg = &q4Config;
    q4ScreenData.ownFont = false;
    q4ScreenData.tab = q4TabMaterial;
    q4ScreenData.cursor = 0;
    q4ScreenData.helpPage = 0;
    q4ScreenData.helpScroll = 0;
    memset(q4ScreenData.labels, 0, sizeof(q4ScreenData.labels));
    q4ConfigSetDefaults(q4Config);
    q4ScreenReset(q4ScreenData);

    // Controls are drawn in the reverse of the order they are created in,
    // so the background (created last) is drawn first.
    static constexpr Q4ScreenPart kOrder[q4ScreenPartCount] = {
        q4ScreenLabels, q4ScreenRight, q4ScreenLeft, q4ScreenHeader, q4ScreenBackground,
    };
    for (Q4ScreenPart part : kOrder)
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
        prop.data = &q4ScreenData;
        screenPart[part] = display.createControl<cQ4Screen>(&prop);
    }
}

void cQ4::showScreen()
{
    titleBar->hide();
    q4ScreenData.ownFont = ownFont;
    refreshAll();
    display.synchronizeRefresh();
}

void cQ4::refreshPart(uint8_t part)
{
    if (part < q4ScreenPartCount)
        display.refreshControl(screenPart[part]);
}

void cQ4::refreshAll()
{
    for (uint8_t part = 0; part < q4ScreenPartCount; part++)
        refreshPart(part);
}
