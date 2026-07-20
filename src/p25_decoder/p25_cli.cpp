#include "p25_cli.hpp"
#include "p25_decoder.hpp"

using namespace Digiham::P25;

int main(int argc, char** argv) {
    Cli runner;
    return runner.main(argc, argv);
}

Digiham::Decoder* Cli::buildModule() {
    auto decoder = new Decoder();
    decoder->setMetaWriter(metaWriter);
    return decoder;
}

std::string Cli::getName() {
    return "p25_decoder";
}
