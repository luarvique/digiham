#pragma once

#include "cli.hpp"

namespace Digiham::P25 {

    class Cli: public Digiham::DecoderCli {
        protected:
            std::string getName() override;
            Decoder* buildModule() override;
    };

}
