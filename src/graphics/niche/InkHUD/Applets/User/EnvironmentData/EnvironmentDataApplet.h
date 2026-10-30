#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Adapted from NodeListApplet which drawing functions I did
not want to abstract.

    +-------------------------------+
    |  SHRT   TM.P°C HUM% BARO.PhPA |
    |  Long name            IAQ 123 |
    |                               |
    |  ABCD   24.5°C 45% 1007.6hPa  |
    |  abcdedfghijk          IAQ 84 |
    |                               |
    +-------------------------------+

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

#include "main.h"

namespace NicheGraphics::InkHUD
{

class EnvironmentDataApplet : public Applet, public MeshModule
{
  protected:
    // Info needed to draw a node card to the list
    // - generated each time we hear a node
    struct EnvCardInfo {
        // all of those entries are optional
        static constexpr float FLOAT_VALUE_UNKNOWN = -1000;
        NodeNum nodeNum = 0;
        float temperature = FLOAT_VALUE_UNKNOWN;
        float relHumidity = FLOAT_VALUE_UNKNOWN;
        float baroPressure = FLOAT_VALUE_UNKNOWN;
        float gasResistance = FLOAT_VALUE_UNKNOWN; // in mOhm
        int iaq = -1;
    };

  public:
    EnvironmentDataApplet();

    void onActivate() override;
    void onRender(bool full) override;

    bool wantPacket(const meshtastic_MeshPacket *p) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  protected:
    void handleParsed(EnvCardInfo c);
    std::string getHeaderText();

    uint8_t maxCards(); // Max number of cards which could ever fit on screen

    void updateCard(NodeNum from, const meshtastic_EnvironmentMetrics& metrics);

    std::deque<EnvCardInfo> cards; // Cards to be rendered.

  private:
    // Card Dimensions
    // - for rendering and for maxCards calc
    uint8_t cardMarginH = 1; // Gap between cards (minimal to fit more rows)
    uint16_t cardH = fontMedium.lineHeight() + fontSmall.lineHeight() + cardMarginH; // Height of card
};

} // namespace NicheGraphics::InkHUD

#endif
