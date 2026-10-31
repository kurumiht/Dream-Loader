// Compile-only ABI layout test. Windows/CI:  g++ -std=c++20 -I include/dreamkurumiHT -c abi/test_abi_layout.cpp
// (static_asserts do the work; the program just reports that it compiled.)
#include "dreamkurumiHT_plugin.h"
#include "abi_layout_asserts.hpp"
#include <cstdio>
int main() { std::puts("ABI v1 layout asserts compiled: OK"); return 0; }
