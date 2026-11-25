// The 1 MB flash is nearly full and this screen is not time-critical.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#include "soti/soti.h"

#include "cr_section_macros.h"
#include "soti/sotiScreen.h"

// What the screen shows; the display controls keep a pointer to it.
__NOINIT(EXTERNAL_RAM) SotiScreenData sotiScreenData;

void cSoti::initDisplayControls()
{
    sotiConfigSetDefaults(sotiConfig);
    sotiScreenData.cfg = &sotiConfig;
    sotiScreenData.ownFont = false;
    sotiScreenData.focus = 0;
    sotiScreenData.fileCursor = 0;
    sotiScreenData.controlCursor = 0;
    sotiScreenReset(sotiScreenData);

    // Controls are drawn in the reverse of the order they are created in,
    // so the background (created last) is drawn first.
    static constexpr SotiScreenPart kOrder[sotiScreenPartCount] = {
        sotiScreenLabels, sotiScreenRight, sotiScreenLeft, sotiScreenHeader, sotiScreenBackground,
    };
    for (SotiScreenPart part : kOrder)
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
        prop.data = &sotiScreenData;
        screenPart[part] = display.createControl<cSotiScreen>(&prop);
    }
}

void cSoti::showScreen()
{
    titleBar->hide();
    sotiScreenData.ownFont = ownFont;
    refreshAll();
    display.synchronizeRefresh();
}

void cSoti::refreshPart(uint8_t part)
{
    if (part < sotiScreenPartCount)
        display.refreshControl(screenPart[part]);
}

void cSoti::refreshAll()
{
    for (uint8_t part = 0; part < sotiScreenPartCount; part++)
        refreshPart(part);
}
