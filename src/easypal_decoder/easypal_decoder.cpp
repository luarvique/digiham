#include "easypal_decoder.hpp"
#include "receiver.hpp"

#include <algorithm>
#include <cstdint>

using namespace Digiham::EasyPal;

// input samples handed to the receiver at once
#define EASYPAL_INPUT_CHUNK 256

Decoder::Decoder(bool raw):
    raw(raw),
    receiver(new Receiver()) {
    receiver->onFile = [this](const File& file) {
        queueFile(file);
    };
}

Decoder::~Decoder() {
    delete receiver;
}

bool Decoder::canProcess() {
    std::lock_guard<std::mutex> lock(processMutex);
    if (writer->writeable() == 0) return false;
    return !pending.empty() || reader->available() > 0;
}

void Decoder::process() {
    std::lock_guard<std::mutex> lock(processMutex);

    // first hand over output that did not fit into the buffer earlier
    flush();

    // do not take any more input while output is backed up
    if (!pending.empty()) return;

    size_t length = reader->available();
    if (length > EASYPAL_INPUT_CHUNK) length = EASYPAL_INPUT_CHUNK;
    if (length == 0) return;

    receiver->push(reader->getReadPointer(), length);
    reader->advance(length);

    flush();
}

void Decoder::flush() {
    size_t length = writer->writeable();
    if (length > pending.size()) length = pending.size();
    if (length == 0) return;

    unsigned char* output = writer->getWritePointer();
    for (size_t i = 0; i < length; i++) {
        output[i] = pending.front();
        pending.pop_front();
    }
    writer->advance(length);
}

void Decoder::queueFile(const File& file) {
    if (!raw) {
        size_t nameLength = std::min<size_t>(file.name.size(), 255);
        size_t callLength = std::min<size_t>(file.callsign.size(), 255);
        uint32_t length = file.data.size();

        pending.insert(pending.end(), {'E', 'P', 'A', 'L'});
        for (int i = 0; i < 4; i++) pending.push_back((length >> (8 * i)) & 0xFF);
        pending.push_back(nameLength);
        pending.insert(pending.end(), file.name.begin(), file.name.begin() + nameLength);
        pending.push_back(callLength);
        pending.insert(pending.end(), file.callsign.begin(), file.callsign.begin() + callLength);
    }
    pending.insert(pending.end(), file.data.begin(), file.data.end());
}
