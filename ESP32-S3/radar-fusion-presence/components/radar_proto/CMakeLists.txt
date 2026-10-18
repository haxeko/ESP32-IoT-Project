# Protocol layer of the HLK-LD2410 / HLK-LD2450 radars: framing, report and ACK
# decoding, command building. No ESP-IDF APIs, so test/host builds the same
# sources with the host compiler.
idf_component_register(SRCS "hlk_frame.cpp" "ld2410_proto.cpp" "ld2450_proto.cpp"
                       INCLUDE_DIRS "include")
