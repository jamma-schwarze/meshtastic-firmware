#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "gps/RTC.h"

#include "GeoCoord.h"
#include "NodeDB.h"

#include "./EnvironmentDataApplet.h"

using namespace NicheGraphics;

InkHUD::EnvironmentDataApplet::EnvironmentDataApplet() : MeshModule("Environment Data")
{
    // we only display data intended for us - don't spy on others ;)
    // MeshModule::isPromiscuous = true;
}

InkHUD::EnvironmentDataApplet::EnvironmentDataApplet::EnvCardInfo::EnvCardInfo() {
}

InkHUD::EnvironmentDataApplet::EnvironmentDataApplet::EnvCardInfo::EnvCardInfo(NodeNum node) : nodeNum(node)
{
}

// since air quality is different from EnvironmentMetrics, we need to use this
bool InkHUD::EnvironmentDataApplet::EnvironmentDataApplet::EnvCardInfo::mergeTelemetry(
    const meshtastic_Telemetry &telemetry
)
{
    bool changed = false;

    if (telemetry.which_variant == meshtastic_Telemetry_environment_metrics_tag) {
        return mergeEnvMetrics(telemetry.variant.environment_metrics);
    } else if (telemetry.which_variant == meshtastic_Telemetry_air_quality_metrics_tag) {
        const meshtastic_AirQualityMetrics &metrics = telemetry.variant.air_quality_metrics;
        changed |= this->has_co2 != metrics.has_co2;
        if (metrics.has_co2) {
            changed |= this->co2 != metrics.co2;
            this->co2 = metrics.co2;
            this->has_co2 = true;
        }
        if (metrics.has_pm_voc_idx) {
            changed |= this->pm_voc_idx != metrics.pm_voc_idx;
            this->pm_voc_idx = metrics.pm_voc_idx;
            this->has_pm_voc_idx = true;
        }
    }
    return changed;
}

bool InkHUD::EnvironmentDataApplet::EnvironmentDataApplet::EnvCardInfo::mergeEnvMetrics(
    const meshtastic_EnvironmentMetrics &metrics
)
{
    bool changed = false;
    changed |= this->has_barometric_pressure != metrics.has_barometric_pressure;
    if (metrics.has_barometric_pressure) {
        changed |= this->barometric_pressure != metrics.barometric_pressure;
        this->barometric_pressure = metrics.barometric_pressure;
        this->has_barometric_pressure = true;
    }
    changed |= this->has_temperature != metrics.has_temperature;
    if (metrics.has_temperature) {
        changed |= this->temperature != metrics.temperature;
        this->temperature = metrics.temperature;
        this->has_temperature = true;
    }
    changed |= this->has_relative_humidity != metrics.has_relative_humidity;
    if (metrics.has_relative_humidity) {
        changed |= this->relative_humidity != metrics.relative_humidity;
        this->relative_humidity = metrics.relative_humidity;
        this->has_relative_humidity = true;
    }
    changed |= this->has_gas_resistance != metrics.has_gas_resistance;
    if (metrics.has_gas_resistance) {
        changed |= this->gas_resistance != metrics.gas_resistance;
        this->gas_resistance = metrics.gas_resistance;
        this->has_gas_resistance = true;
    }
    changed |= this->has_iaq != metrics.has_iaq;
    if (metrics.has_iaq) {
        changed |= this->iaq != metrics.iaq;
        this->iaq = metrics.iaq;
        this->has_iaq = true;
    }

    return changed;
}

void InkHUD::EnvironmentDataApplet::EnvironmentDataApplet::EnvCardInfo::logDebug()
{
    LOG_DEBUG("EnvData for %08x", this->nodeNum);
    //TODO
}

void InkHUD::EnvironmentDataApplet::onActivate()
{
    // also display our own telemetry
    MeshModule::loopbackOk = true;

    // initialize our cards to show the telemetry Meshtastic already knows about
    // note: air quality metrics are not retained in NodeDB AFAIK

    std::deque<std::pair<NodeNum, meshtastic_Telemetry>> lastNodesWithEnvData;
    const uint8_t max = maxCards();

    // getMeshNodeByIndex returns in last seen order with favorites on top - limit to max number we can show
    for (size_t i = 0; i < nodeDB->getNumMeshNodes() && lastNodesWithEnvData.size() < max; i++) {  
        const meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);  
        meshtastic_EnvironmentMetrics env;  
        if (nodeDB->copyNodeEnvironment(n->num, env)) {
            // contruct a telemetry here since we use this across the class
            meshtastic_Telemetry tel;
            tel.which_variant = meshtastic_Telemetry_environment_metrics_tag;
            tel.variant.environment_metrics = env;
            lastNodesWithEnvData.emplace_back(n->num, tel);
        }
            
    }

    LOG_INFO("%s initialized: %d nodes of %d with telemetry from NodeDB", this->MeshModule::name, 
        lastNodesWithEnvData.size(), nodeDB->getNumMeshNodes());

    // process all found nodes last to first because we insert them at the top
    for (auto node = lastNodesWithEnvData.rbegin(); node != lastNodesWithEnvData.rend(); ++node) {
        updateCard(node->first, node->second);
    }
}

void InkHUD::EnvironmentDataApplet::onDeactivate()
{
    // reduce packets received
    MeshModule::loopbackOk = false;
}

// Do we want to process this packet with handleReceived()?
bool InkHUD::EnvironmentDataApplet::wantPacket(const meshtastic_MeshPacket *p)
{
    // Only interested in telemetry
    return isActive()                                                  // Applet is active
           && p->decoded.portnum == meshtastic_PortNum_TELEMETRY_APP;  // and telemetry
}

// MeshModule packets arrive here
ProcessMessage InkHUD::EnvironmentDataApplet::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Abort if applet fully deactivated
    // Already handled by wantPacket in this case, but good practice for all applets, as some *do* require this early return
    if (!isActive())
        return ProcessMessage::CONTINUE;

    // Assemble info: from nodeDB (needed to detect changes)
    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(mp.from);
    if (!node) {
        return ProcessMessage::CONTINUE; // Let others look at this message also if they want
    }

    // we only get decoded telemetry packets here (wantPacket ensures this)
    meshtastic_Telemetry t;
    if (! (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size,  
            &meshtastic_Telemetry_msg, &t))
           && ((t.which_variant == meshtastic_Telemetry_environment_metrics_tag)
              || (t.which_variant == meshtastic_Telemetry_air_quality_metrics_tag))) {
        return ProcessMessage::CONTINUE; // Let others look at this message also if they want    
    }

    updateCard(mp.from, t);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}
    
void InkHUD::EnvironmentDataApplet::updateCard(NodeNum from, const meshtastic_Telemetry& telemetry)
{
    EnvCardInfo card;
    int previousPosition = -1; // index of previous card (if found)
    int pos = 0;
    // check whether we already have data about this node and remove it from list
    for (auto it = cards.begin(); it != cards.end(); ++it) {
        if (it->nodeNum == from) {
            // retain old values for updating
            card = *it;
            previousPosition = pos;
            // remove - we'll reinsert below
            cards.erase(it);
            break;
        }
        ++pos;
    }

    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(from);
    if (! node) {
        // shouldn't happen if we just received a packet, but prevent NULL dereference
        if (previousPosition >= 0) {
            requestUpdate();
        }
        return;
    }

    bool isFavorite = nodeInfoLiteIsFavorite(node) || (from == nodeDB->getNodeNum());

    LOG_INFO("%s processing telemetry from !%08x %s", this->MeshModule::name, from, isFavorite ? " (favorite)" : "");
    boolean needsUpdate = (card.nodeNum == 0) || (card.isFavorite != isFavorite);
    // now copy new values (note: card might have been empty if we didn't have data for that node cached)
    card.nodeNum = from;
    card.isFavorite = isFavorite;
    needsUpdate |= card.mergeTelemetry(telemetry);

    // look for correct position of card
    // favorites first (last seen first), then non-favorites
    if (card.isFavorite) {
        LOG_INFO("%s putting favorite !%08x on top", this->MeshModule::name, from);
        cards.push_front(card); // Store this CardInfo
    } else {
        pos = 0;
        auto it = cards.begin();
        // skip all favorites
        while (it != cards.end() && it->isFavorite) {
            ++it;
            ++pos;
        }
        LOG_INFO("%s inserting updated card for !%08x at pos. %d", this->MeshModule::name, from, pos);
        // insert after last favorite (which might be last card too)
        cards.insert(it, card);
        needsUpdate |= (pos != previousPosition);
    }

    if (cards.size() > maxCards()) {
        LOG_INFO("%s resizing cards cache to %d", this->MeshModule::name, maxCards());
        cards.resize(maxCards()); // Don't keep more cards than we could *ever* fit on screen
    }

    if (needsUpdate) {
        requestAutoshow(); // user decides anyway
        requestUpdate();
    }

}

// Calculate maximum number of cards we may ever need to render, in our tallest layout config
// Number might be slightly in excess of the true value: applet header text not accounted for
uint8_t InkHUD::EnvironmentDataApplet::maxCards()
{
    // Cache result. Shouldn't change during execution
    static uint8_t maxCardCount = 0;

    if (!maxCardCount) {
        const uint16_t height = Tile::maxDisplayDimension();

        // Use a loop instead of arithmetic, because it's easier for my brain to follow
        // Add cards one by one, until the latest card extends below screen

        uint16_t y = cardH; // First card: no margin above
        maxCardCount = 1;

        while (y < height) {
            y += cardMarginH;
            y += cardH;
            maxCardCount++;
        }
    }

    return maxCardCount;
}

std::string InkHUD::EnvironmentDataApplet::getHeaderText()
{
    std::string text;

    // Print the node count
    const uint16_t nodeCount = cards.size();
    text += ": ";
    text += to_string(nodeCount);
    text += " ";
    text += (nodeCount == 1) ? "node" : "nodes";

    return text;

}

// Draw, using info placed into this->cards for us
void InkHUD::EnvironmentDataApplet::onRender(bool full)
{

    // ================================
    // Draw the standard applet header
    // ================================

    drawHeader(getHeaderText()); // Ask derived applet for the title

    // Dimensions of the header
    int16_t headerDivY = getHeaderHeight() - 1;
    constexpr uint16_t padDivH = 2;

    // ========================
    // Draw the main node list
    // ========================

    // Leave a small gutter between long-name text and right-side card content
    constexpr uint8_t rightContentGap = 2;

    // Truncate with trailing "...", sized using the current font.
    auto ellipsizeToWidth = [this](std::string text, uint16_t maxWidth) {
        constexpr const char *ellipsis = "...";
        const uint16_t ellipsisW = getTextWidth(ellipsis);
        uint16_t textW = getTextWidth(text);
        if (maxWidth == 0)
            return std::string();
        if (textW <= maxWidth)
            return text;
        if (ellipsisW > maxWidth)
            return std::string();
        while (!text.empty() && (textW + ellipsisW > maxWidth)) {
            text.pop_back();
            textW = getTextWidth(text);
        }
        return text + ellipsis;
    };

    // Y value (top) of the current card. Increases as we draw.
    uint16_t cardTopY = headerDivY + padDivH;

    // -- Each node in list --
    for (auto card = cards.begin(); card != cards.end(); ++card) {

        // Gather info
        // ========================================
        const NodeNum &nodeNum = card->nodeNum;
        std::string longName;  // handled below
        std::string shortName; // handled below
        String tempEtc = ""; // handled below
        String airq = ""; // handled below

        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeNum);

        // Skip deleted nodes
        if (!node) {
            cards.erase(card);
            continue;
        }

        // -- Shortname --
        // Parse special chars in the short name
        // Use "?" if unknown
        if (node)
            shortName = parseShortName(node);
        else
            shortName = "?";

        // -- Longname --
        // Parse special chars in long name
        // Use node id if unknown
        if (nodeInfoLiteHasUser(node))
            longName = parse(node->long_name); // Found in nodeDB
        else {
            // Not found in nodeDB, show a hex nodeid instead
            longName = hexifyNodeNum(nodeNum);
        }

        if (card->has_temperature) {
            tempEtc += String(card->temperature, 1) + "C";
        }
        if (card->has_relative_humidity) {
            if (tempEtc.length() > 0) { tempEtc += " "; }
            tempEtc += String(card->relative_humidity, 1) + "%";
        }
        if (card->has_barometric_pressure) {
            if (tempEtc.length() > 0) { tempEtc += " "; }
            tempEtc += String(card->barometric_pressure, 1) + "hPa";
        }

        // AirQuality
        if (card->has_co2) {
            airq += String(card->co2)+"Co2";
        }
        if (card->has_iaq) {
            if (airq.length() > 0) { airq += " ";}
            airq += "IAQ "+String(card->iaq);
        }

        // Draw the info
        // ====================================

        // Define two lines of text for the card
        // We will center our text on these lines
        uint16_t lineAY = cardTopY + (fontMedium.lineHeight() / 2);
        uint16_t lineBY = cardTopY + fontMedium.lineHeight() + (fontSmall.lineHeight() / 2);

        // Print the short name
        setFont(fontMedium);
        printAt(0, lineAY, shortName, LEFT, MIDDLE);

        // Right-side labels and long name are rendered in small font.
        setFont(fontSmall);
        uint16_t rightContentW = 0;

        // Bottom row right: iaq.
        if (airq.length() > 0) {
            rightContentW = std::max(rightContentW, getTextWidth(airq.c_str()));
            printAt(width() - 1, lineBY, airq.c_str(), RIGHT, MIDDLE);
        }

        // Top row right: temperature etc.
        if (tempEtc.length() > 0) {
            //rightContentW = std::max(rightContentW, getTextWidth(tempEtc.));
            printAt(width() - 1, lineAY, tempEtc.c_str(), RIGHT, MIDDLE);
        }

        // Give long names as much room as possible while still avoiding right side
        const uint16_t longNameMaxW =
            (rightContentW + rightContentGap < width()) ? (width() - rightContentW - rightContentGap) : 0;
        const std::string longNameShown = ellipsizeToWidth(longName, longNameMaxW);

        // Safety crop
        setCrop(0, cardTopY, longNameMaxW, cardH);
        printAt(0, lineBY, longNameShown, LEFT, MIDDLE);

        resetCrop();

        // Draw separator between cards
        const int16_t separatorY = cardTopY + cardH - 1;
        if (separatorY < height() - 1) {
            for (int16_t xSep = 0; xSep < width(); xSep += 2)
                drawPixel(xSep, separatorY, BLACK);
        }

        // Prepare to draw the next card
        cardTopY += cardH;

        // Once we've run out of screen, stop drawing cards
        // Depending on tiles / rotation, this may be before we hit maxCards
        if (cardTopY > height())
            break;
    }
}

#endif
