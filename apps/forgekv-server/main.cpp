#include "forgekv/version.hpp"

#include <iostream>

int main() {
    std::cout << "forgekv-server " << forgekv::version()
              << " (Milestone 0 skeleton; networking is not implemented)\n";
    return 0;
}

