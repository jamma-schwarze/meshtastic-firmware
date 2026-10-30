#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "gps/RTC.h"

#include "GeoCoord.h"
#include "NodeDB.h"

#include "./EnvironmentDataApplet.h"

using namespace NicheGraphics;

InkHUD::EnvironmentDataApplet::EnvironmentDataApplet() : MeshModule("Environment Data")
{
    // we only display data intended for us
    // MeshModule::isPromiscuous = true;
    // also display our own telemetry
    MeshModule::loopbackOk = true;
}

void InkHUD::EnvironmentDataApplet::onActivate()
{
    // initialize our cards to show the telemetry Meshtastic already knows about

    std::deque<std::pair<NodeNum, meshtastic_EnvironmentMetrics>> lastNodesWithEnvData;
    const uint8_t max = maxCards();

    // getMeshNodeByIndex returns in last seen order with favorites on top - limit to max number we can show
    for (size_t i = 0; i < nodeDB->getNumMeshNodes() && lastNodesWithEnvData.size() < max; i++) {  
        const meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);  
        meshtastic_EnvironmentMetrics env;  
        if (nodeDB->copyNodeEnvironment(n->num, env))  
            lastNodesWithEnvData.emplace_back(n->num, env);
    }

    LOG_INFO("Environment Data initialized: %d nodes of %d with telemetry from NodeDB", this->MeshModule::name, 
        lastNodesWithEnvData.size(), nodeDB->getNumMeshNodes());

    // process all found nodes last to first because we insert them at the top
    for (auto node = lastNodesWithEnvData.rbegin(); node != lastNodesWithEnvData.rend(); ++node) {
        updateCard(node->first, node->second);
    }
}

// Do we want to process this packet with handleReceived()?
bool InkHUD::EnvironmentDataApplet::wantPacket(const meshtastic_MeshPacket *p)
{
    // Only interested if: TODO: do we get our own telemetry this way?
    return isActive()                                                  // Applet is active
           //&& !isFromUs(p)                                             // Packet is incoming (not outgoing)
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
           && (t.which_variant == meshtastic_Telemetry_environment_metrics_tag)) {
        return ProcessMessage::CONTINUE; // Let others look at this message also if they want    
    }

    const auto &metrics = t.variant.environment_metrics;  

    updateCard(mp.from, metrics);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}
    
void InkHUD::EnvironmentDataApplet::updateCard(NodeNum from, const meshtastic_EnvironmentMetrics &metrics)
{
    EnvCardInfo c;
    c.nodeNum = from;

    NodeNum firstNodeNum = 0;
    if (! cards.empty()) {
        firstNodeNum = cards.at(0).nodeNum;
    }

    EnvCardInfo previous;
    // check whether we already have data about this node
    for (auto it = cards.begin(); it != cards.end(); ++it) {
        if (it->nodeNum == c.nodeNum) {
            previous = *it;
            // remove - we'll reinsert below
            cards.erase(it);
            break;
        }
    }

    boolean needsUpdate = firstNodeNum != previous.nodeNum;

    // use m.has_temperature / m.temperature, etc.  
    if (metrics.has_temperature) {
        c.temperature = metrics.temperature;
        needsUpdate |= (previous.temperature != c.temperature);
    }
    if (metrics.has_relative_humidity) {
        c.relHumidity = metrics.relative_humidity;
        needsUpdate |= (previous.relHumidity != c.relHumidity);
    }
    if (metrics.has_barometric_pressure) {
        c.baroPressure = metrics.barometric_pressure;
        needsUpdate |= (previous.baroPressure != c.baroPressure);
    }
    if (metrics.has_gas_resistance) {
        c.gasResistance = metrics.gas_resistance;
        needsUpdate |= (previous.gasResistance != c.gasResistance);
    }
    if (metrics.has_iaq) {
        c.iaq = metrics.iaq;
        needsUpdate |= (previous.iaq != c.iaq);
    }

    cards.push_front(c);                                  // Store this CardInfo
    cards.resize(min(maxCards(), (uint8_t)cards.size())); // Don't keep more cards than we could *ever* fit on screen
    cards.shrink_to_fit();

    if (needsUpdate) {
        requestAutoshow(); // TODO: do we want this?
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
        std::string iaq; // handled below

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

        if (card->temperature != EnvCardInfo::FLOAT_VALUE_UNKNOWN) {
            tempEtc += String(card->temperature, 1) + "C";
        }
        if (card->relHumidity != EnvCardInfo::FLOAT_VALUE_UNKNOWN) {
            if (tempEtc.length() > 0) { tempEtc += " "; }
            tempEtc += String(card->relHumidity) + "%";
        }
        if (card->baroPressure != EnvCardInfo::FLOAT_VALUE_UNKNOWN) {
            if (tempEtc.length() > 0) { tempEtc += " "; }
            tempEtc += String(card->baroPressure, 1) + "hPa";
        }

        // IAQ
        if (card->iaq >= 0) {
            iaq = "IAQ "+to_string(card->iaq);
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
        if (!iaq.empty()) {
            rightContentW = std::max(rightContentW, getTextWidth(iaq));
            printAt(width() - 1, lineBY, iaq, RIGHT, MIDDLE);
        }

        // Top row right: temperature etc.
        if (tempEtc.length() > 0) {
            //rightContentW = std::max(rightContentW, getTextWidth(tempEtc.));
            const std::string tempEtcStr(tempEtc.c_str());
            printAt(width() - 1, lineAY, tempEtcStr, RIGHT, MIDDLE);
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
        if (separatorY < height() - 1 && (card + 1) != cards.end()) {
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
