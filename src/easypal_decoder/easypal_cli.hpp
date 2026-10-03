#pragma once

#include "cli.hpp"

namespace Digiham::EasyPal {

    class Cli: public Digiham::Cli<float, unsigned char> {
        protected:
            std::string getName() override;
            Csdr::Module<float, unsigned char>* buildModule() override;
            std::stringstream getUsageString() override;
            std::vector<struct option> getOptions() override;
            bool receiveOption(int c, char* optarg) override;

        private:
            bool raw = false;
    };

}
