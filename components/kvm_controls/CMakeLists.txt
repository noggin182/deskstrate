# What a deck key or keyboard Fn key asks the core for, given the core's STATE. Plain
# C++ with no ESP-IDF, so test/ builds and runs it on a PC.
idf_component_register(SRCS "kvm_controls.cpp" INCLUDE_DIRS "include" REQUIRES kvm_proto)
