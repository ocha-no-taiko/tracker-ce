#include "qtrl/qtrl.h"

#include <string.h>

#include "cr_section_macros.h"
#include "qtrl/qtrlScreen.h"

// What the screen shows; the display controls keep a pointer to it.
__NOINIT(EXTERNAL_RAM) QtrlScreenData qtrlScreenData;

void cQtrl::initDisplayControls()
{
    qtrlScreenData.cfg = &qtrlConfig;
    qtrlScreenData.ownFont = false;
    qtrlScreenData.tab = qtrlTabHome;
    memset(qtrlScreenData.labels, 0, sizeof(qtrlScreenData.labels));
    qtrlScreenData.input[0] = '\0';
    qtrlScreenData.inputLength = 0;
    qtrlScreenData.pressedKey = -1;
    qtrlConfigSetDefaults(qtrlConfig);
    qtrlScreenReset(qtrlScreenData);

    // Controls are drawn in the reverse of the order they are created in,
    // so the background (created last) is drawn first.
    static constexpr QtrlScreenPart kOrder[qtrlScreenPartCount] = {
        qtrlScreenLabels, qtrlScreenRight, qtrlScreenLeft, qtrlScreenHeader, qtrlScreenBackground,
    };
    for (QtrlScreenPart part : kOrder)
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
        prop.data = &qtrlScreenData;
        screenPart[part] = display.createControl<cQtrlScreen>(&prop);
    }
}

void cQtrl::showScreen()
{
    titleBar->hide();
    qtrlScreenData.ownFont = ownFont;
    refreshAll();
    display.synchronizeRefresh();
}

void cQtrl::refreshPart(uint8_t part)
{
    if (part < qtrlScreenPartCount)
        display.refreshControl(screenPart[part]);
}

void cQtrl::refreshAll()
{
    for (uint8_t part = 0; part < qtrlScreenPartCount; part++)
        refreshPart(part);
}
