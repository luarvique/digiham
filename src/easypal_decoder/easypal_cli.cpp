#include "easypal_cli.hpp"
#include "easypal_decoder.hpp"

using namespace Digiham::EasyPal;

int main(int argc, char** argv) {
    Cli runner;
    return runner.main(argc, argv);
}

Csdr::Module<float, unsigned char>* Cli::buildModule() {
    return new Decoder(raw);
}

std::string Cli::getName() {
    return "easypal_decoder";
}

std::stringstream Cli::getUsageString() {
    std::stringstream result = Digiham::Cli<float, unsigned char>::getUsageString();
    result << " -r, --raw           write the bare bytes of the received files, instead of\n"
           << "                     records with file name and callsign\n"
           << "\n"
           << "Input is mono audio at 12kHz, as 32bit float.\n";
    return result;
}

std::vector<struct option> Cli::getOptions() {
    std::vector<struct option> options = Digiham::Cli<float, unsigned char>::getOptions();
    options.push_back({"raw", no_argument, NULL, 'r'});
    return options;
}

bool Cli::receiveOption(int c, char* optarg) {
    switch (c) {
        case 'r':
            raw = true;
            break;
        default:
            return Digiham::Cli<float, unsigned char>::receiveOption(c, optarg);
    }
    return true;
}
