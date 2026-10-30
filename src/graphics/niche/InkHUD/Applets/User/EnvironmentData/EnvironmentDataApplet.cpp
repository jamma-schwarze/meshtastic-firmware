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

void InkHUD::EnvironmentDataApplet::onActivate()
{
    // also display our own telemetry
    MeshModule::loopbackOk = true;

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
           && (t.which_variant == meshtastic_Telemetry_environment_metrics_tag)) {
        return ProcessMessage::CONTINUE; // Let others look at this message also if they want    
    }

    const auto &metrics = t.variant.environment_metrics;  

    updateCard(mp.from, metrics);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}
    
void InkHUD::EnvironmentDataApplet::updateCard(NodeNum from, const meshtastic_EnvironmentMetrics &metrics)
{
    // we build a new EnvCardInfo here and discard the previous one
    EnvCardInfo newCard;
    newCard.nodeNum = from;

    EnvCardInfo previousCard;
    int previousPosition = -1; // index of previous card (if found)
    int pos = 0;
    // check whether we already have data about this node and remove it from list
    for (auto it = cards.begin(); it != cards.end(); ++it) {
        if (it->nodeNum == newCard.nodeNum) {
            previousCard = *it;
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

    newCard.isFavorite = nodeInfoLiteIsFavorite(node) || (from == nodeDB->getNodeNum());

    std::string longName; // only used for logging right now
    if (nodeInfoLiteHasUser(node))
        longName = parse(node->long_name); // Found in nodeDB
    else {
        // Not found in nodeDB, show a hex nodeid instead
        longName = hexifyNodeNum(from);
    }

    LOG_INFO("%s processing telemetry from %s %s", this->MeshModule::name, longName, newCard.isFavorite ? " (favorite)" : "");
    boolean needsUpdate = (previousCard.nodeNum != 0) || (previousCard.isFavorite != newCard.isFavorite);
    const meshtastic_EnvironmentMetrics &previousMetrics = previousCard.metrics;
    
    // use m.has_temperature / m.temperature, etc. to copy all metrics relevant to us to new card
    if (metrics.has_temperature) {
        newCard.metrics.temperature = metrics.temperature;
        newCard.metrics.has_temperature = true;
        needsUpdate |= (previousMetrics.temperature != newCard.metrics.temperature);
    }
    if (metrics.has_relative_humidity) {
        newCard.metrics.relative_humidity = metrics.relative_humidity;
        newCard.metrics.has_relative_humidity = true;
        needsUpdate |= (previousMetrics.relative_humidity != newCard.metrics.relative_humidity);
    }
    if (metrics.has_barometric_pressure) {
        newCard.metrics.barometric_pressure = metrics.barometric_pressure;
        newCard.metrics.has_barometric_pressure = true;
        needsUpdate |= (previousMetrics.barometric_pressure != newCard.metrics.barometric_pressure);
    }
    if (metrics.has_gas_resistance) {
        newCard.metrics.has_gas_resistance = metrics.gas_resistance;
        newCard.metrics.has_gas_resistance = true;
        needsUpdate |= (previousMetrics.has_gas_resistance != newCard.metrics.has_gas_resistance);
    }
    if (metrics.has_iaq) {
        newCard.metrics.iaq = metrics.iaq;
        newCard.metrics.has_iaq = true;
        needsUpdate |= (previousMetrics.iaq != newCard.metrics.iaq);
    }

    // look for correct position of card
    // favorites first (last seen first), then non-favorites
    if (newCard.isFavorite) {
        LOG_INFO("%s putting favorite %s on top", this->MeshModule::name, longName);
        cards.push_front(newCard); // Store this CardInfo
    } else {
        pos = 0;
        auto it = cards.begin();
        // skip all favorites
        while (it != cards.end() && it->isFavorite) {
            ++it;
            ++pos;
        }
        LOG_INFO("%s inserting updated card for %s at pos. %d", this->MeshModule::name, longName, pos);
        // insert after last favorite (which might be last card too)
        cards.insert(it, newCard);
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

        if (card->metrics.has_temperature) {
            tempEtc += String(card->metrics.temperature, 1) + "C";
        }
        if (card->metrics.has_relative_humidity) {
            if (tempEtc.length() > 0) { tempEtc += " "; }
            tempEtc += String(card->metrics.relative_humidity, 1) + "%";
        }
        if (card->metrics.has_barometric_pressure) {
            if (tempEtc.length() > 0) { tempEtc += " "; }
            tempEtc += String(card->metrics.barometric_pressure, 1) + "hPa";
        }

        // IAQ
        if (card->metrics.has_iaq) {
            iaq = "IAQ "+to_string(card->metrics.iaq);
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
