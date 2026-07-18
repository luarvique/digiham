#include "p25_decoder.hpp"
#include "p25_meta.hpp"
#include "p25_phase.hpp"

using namespace Digiham::P25;

Decoder::Decoder(): Digiham::Decoder(new SyncPhase, new MetaCollector) {}
