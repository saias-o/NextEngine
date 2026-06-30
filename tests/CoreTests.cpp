#include "core/Easing.hpp"
#include "core/Signal.hpp"

#include <cassert>
#include <cmath>

int main() {
    assert(std::abs(saida::applyEasing(saida::Easing::Linear, 0.25f) - 0.25f) < 1e-6f);

    saida::Signal<int> signal;
    int observed = 0;
    {
        auto connection = signal.connect([&](int value) { observed = value; });
        signal.emit(7);
        assert(observed == 7);
    }
    signal.emit(9);
    assert(observed == 7);
    return 0;
}
