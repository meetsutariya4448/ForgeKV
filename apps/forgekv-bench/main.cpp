#include "forgekv/version.hpp"

#include <iostream>

int main() {
    std::cout << "forgekv-bench " << forgekv::version()
              << " (Milestone 0 skeleton; workloads are not implemented)\n";
    return 0;
}

