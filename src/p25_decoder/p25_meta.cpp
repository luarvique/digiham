#include "p25_meta.hpp"
#include <sstream>

using namespace Digiham::P25;

std::map<std::string, std::string> MetaCollector::collect() {
    auto metadata = Digiham::MetaCollector::collect();

    if (!sync.empty()) {
        metadata["sync"] = sync;
    }

    if (!type.empty()) {
        metadata["type"] = type;
    }

    if (nacValid) {
        std::stringstream ss;
        ss << "0x" << std::hex << nac;
        metadata["nac"] = ss.str();
    }

    if (source != 0) {
        metadata["source"] = std::to_string(source);
    }

    if (destination != 0) {
        metadata["destination"] = std::to_string(destination);
    }

    if (encrypted) {
        metadata["encryption"] = "encrypted";
        metadata["algid"] = std::to_string(algid);
        metadata["kid"] = std::to_string(kid);
    }

    return metadata;
}

std::string MetaCollector::getProtocol() {
    return "P25";
}

void MetaCollector::setSync(std::string sync) {
    if (this->sync == sync) return;
    this->sync = sync;
    sendMetaData();
}

void MetaCollector::setType(std::string type) {
    if (this->type == type) return;
    this->type = type;
    sendMetaData();
}

void MetaCollector::setNac(uint16_t nac) {
    if (nacValid && this->nac == nac) return;
    this->nac = nac;
    nacValid = true;
    sendMetaData();
}

void MetaCollector::setSource(uint32_t source) {
    if (this->source == source) return;
    this->source = source;
    sendMetaData();
}

void MetaCollector::setDestination(uint32_t destination) {
    if (this->destination == destination) return;
    this->destination = destination;
    sendMetaData();
}

void MetaCollector::setEncrypted(bool encrypted, uint8_t algid, uint16_t kid) {
    if (this->encrypted == encrypted && this->algid == algid && this->kid == kid) return;
    this->encrypted = encrypted;
    this->algid = algid;
    this->kid = kid;
    sendMetaData();
}

void MetaCollector::reset() {
    hold();
    setSync("");
    setType("");
    setSource(0);
    setDestination(0);
    setEncrypted(false, 0, 0);
    // deliberately keep NAC: it identifies the system and rarely changes
    release();
}
