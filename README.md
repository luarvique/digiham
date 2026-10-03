# Digital Ham Radio decoding tools

This is a simple set of command-line tools that is intended to be used to decode digital modulations used by ham radio
operators. The main focus is on digital voice modes.

Right now this project enables you to decode DMR and YSF, future plans include NXDN and D-Star.

The main use of this project is to run in the backend of [OpenWebRX](https://github.com/jketterl/openwebrx), where it
decodes the available information, which is then displayed on the receiver's website.

## Requirements

Please make sure you install the following dependencies before compiling digiham:

- [csdr](https://github.com/jketterl/csdr) (version 0.18 or later)
- [codecserver](https://github.com/jketterl/codecserver)
- [ICU](https://icu.unicode.org/) (for Debian, install `libicu-dev`)
- [FFTW](https://www.fftw.org/) (single precision; for Debian, install `libfftw3-dev`)


## About the AMBE codec

Most digital voice modes in the ham radio universe right now use some version of the AMBE digital voice codec. In order
to decode them, you will need to setup the correspoding decoding infrastructure.

This project comes with mbe_synthesizer that can send the received audio data to a
[codecserver](https://github.com/jketterl/codecserver) instance for decoding.

## EasyPal (digital SSTV)

`easypal_decoder` decodes EasyPal transmissions, which are HamDRM: a DRM mode in a ~2.4kHz audio channel that carries
files, usually JPEG images. It takes mono audio at 12kHz as 32bit float (for example the output of an upper sideband
demodulator) and writes out every file that has been received completely. It supports DRM robustness modes A and B, the
2.3kHz and 2.5kHz occupancies, 4, 16 and 64 point QAM and both interleaver depths.

By default, every file is written as a record that also carries the file name and the callsign of the sender (see
`include/easypal_decoder.hpp` for the layout). With `--raw`, only the bytes of the files are written, so a single image
can be saved with `easypal_decoder --raw > image.jpg`. Files protected with Reed-Solomon (`.rs1` to `.rs4`) are written
as they were received, they are not decoded.

The receiver was written from the DRM format (ETSI ES 201 980) and its HamDRM extension as they are implemented by
[Dream](https://sourceforge.net/projects/drm/) and [QSSTV](https://github.com/ON4QZ/QSSTV), both of which are GPL
projects. No code was copied from them, but the constants in `src/easypal_decoder/tables.hpp` come from there.

## Installation

This project comes with a cmake build. It is recommended to build in a separate directory.

```
mkdir build
cd build
cmake ..
make
sudo make install
```

## Examples

You can find shell scripts that show the basic usage of the components in the `examples` folder.
