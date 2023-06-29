#include <iostream>

#include "../include/atem.h"

int main(int argc, char **argv) {
  std::cout << "ATEM Communication for ESP-IDF (on linux)" << std::endl;

  new atem::Atem("10.0.2.194");

  return 0;
}
