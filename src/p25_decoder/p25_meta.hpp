#pragma once

#include "meta.hpp"
#include <string>
#include <cstdint>

namespace Digiham::P25 {

    class MetaCollector: public Digiham::MetaCollector {
        public:
            void setSync(std::string sync);
            void setType(std::string type);
            void setNac(uint16_t nac);
            void setSource(uint32_t source);
            void setDestination(uint32_t destination);
            void setManufacturerId(uint8_t mfid);
            void setEncrypted(bool encrypted, uint8_t algid, uint16_t kid);
            void reset();
        protected:
            std::map<std::string, std::string> collect() override;
            std::string getProtocol() override;
        private:
            std::string sync;
            std::string type;
            uint16_t nac = 0;
            bool nacValid = false;
            uint32_t source = 0;
            uint32_t destination = 0;
            bool encrypted = false;
            uint8_t algid = 0;
            uint16_t kid = 0;
            uint8_t mfid = 0;
    };

}
