add_library(digiham_easypal_decoder OBJECT easypal_decoder.cpp receiver.cpp ofdm.cpp frame_layout.cpp fec.cpp mot.cpp)
target_compile_options(digiham_easypal_decoder PRIVATE "-fPIC")
target_include_directories(digiham_easypal_decoder PRIVATE ${FFTW3F_INCLUDE_DIRS})

add_executable(easypal_decoder easypal_cli.cpp)
target_link_libraries(easypal_decoder digiham)
install(TARGETS easypal_decoder DESTINATION ${CMAKE_INSTALL_BINDIR})
