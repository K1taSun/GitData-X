#include <iostream>

#include "core/version.hpp"

int main() {
    std::cout << "GitData X v" << gitdatax::version() << '\n';
    return 0;
}

