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
    class EnvCardInfo {
      public:
        EnvCardInfo();
        EnvCardInfo(NodeNum node);
        // update this card with given metrics
        // return true if there where changes
        bool mergeTelemetry(const meshtastic_Telemetry &telemetry);
        // if we got EnvironmentMetrics already
        bool mergeEnvMetrics(const meshtastic_EnvironmentMetrics &metrics);
        void logDebug();
        NodeNum nodeNum = 0;
        bool isFavorite = false;
        // we partly duplicate meshtastic_EnvironmentMetrics here
        bool has_temperature = false;
        float temperature;
        bool has_relative_humidity = false;
        float relative_humidity;
        bool has_barometric_pressure = false;
        float barometric_pressure;
        bool has_gas_resistance = false;
        float gas_resistance;
        bool has_iaq = false;
        uint16_t iaq;
        bool has_co2 = false;
        uint32_t co2;
        bool has_pm_voc_idx = false;
        float pm_voc_idx;
    };

  public:
    EnvironmentDataApplet();

    void onActivate() override;
    void onDeactivate() override;
    void onRender(bool full) override;

    bool wantPacket(const meshtastic_MeshPacket *p) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  protected:
    void handleParsed(EnvCardInfo c);
    std::string getHeaderText();

    uint8_t maxCards(); // Max number of cards which could ever fit on screen

    void updateCard(NodeNum from, const meshtastic_Telemetry& telemetry);

    std::list<EnvCardInfo> cards; // Cards to be rendered.

  private:
    // Card Dimensions
    // - for rendering and for maxCards calc
    uint8_t cardMarginH = 1; // Gap between cards (minimal to fit more rows)
    uint16_t cardH = fontMedium.lineHeight() + fontSmall.lineHeight() + cardMarginH; // Height of card
};

} // namespace NicheGraphics::InkHUD

#endif
