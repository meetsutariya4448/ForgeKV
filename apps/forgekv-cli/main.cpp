#include "forgekv/version.hpp"

#include <iostream>

int main() {
    std::cout << "forgekv-cli " << forgekv::version()
              << " (Milestone 0 skeleton; commands are not implemented)\n";
    return 0;
}

