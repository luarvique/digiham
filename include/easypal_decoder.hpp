#pragma once

#include <csdr/module.hpp>

#include <deque>

namespace Digiham {

    namespace EasyPal {

        // private API
        class Receiver;
        struct File;

        // Decodes EasyPal ("digital SSTV") transmissions. EasyPal is HamDRM: a DRM
        // COFDM mode in a ~2.4kHz audio channel that carries files, usually JPEG
        // images.
        //
        // Input is mono audio with a sample rate of 12kHz, as 32bit float.
        //
        // Output is one record for every file that has been received completely.
        // By default, a record consists of
        //
        //   "EPAL"       4 bytes, magic
        //   length       4 bytes, little endian, size of the file
        //   nameLength   1 byte
        //   name         file name as announced by the sender (may be empty)
        //   callLength   1 byte
        //   callsign     callsign of the sender (may be empty)
        //   data         <length> bytes
        //
        // In raw mode, only the bytes of the files are written.
        class Decoder: public Csdr::Module<float, unsigned char> {
            public:
                explicit Decoder(bool raw = false);
                ~Decoder() override;
                bool canProcess() override;
                void process() override;

            private:
                void queueFile(const File& file);
                void flush();
                bool raw;
                Receiver* receiver;
                std::deque<unsigned char> pending;
        };

    }

}
