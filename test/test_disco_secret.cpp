#include "../src/disco_secret.h"

#include <cassert>

int main() {
    DiscoSecret secret;
    assert(!secret.Input(L'd', 100));
    assert(!secret.Input(L'I', 300));
    assert(!secret.Input(L's', 500));
    assert(!secret.Input(L'c', 700));
    assert(secret.Input(L'O', 900));

    assert(!secret.Input(L'D', 1000));
    assert(!secret.Input(L'I', 1100));
    assert(!secret.Input(L'S', 1200));
    assert(!secret.Input(L'C', 1300));
    assert(secret.Input(L'O', 1400));

    assert(!secret.Input(L'D', 2000));
    assert(!secret.Input(L'I', 5101));
    assert(!secret.Input(L'S', 5200));
    assert(!secret.Input(L'C', 5300));
    assert(!secret.Input(L'O', 5400));

    secret.Reset();
    assert(!secret.Input(L'D', 6000));
    assert(!secret.Input(L'I', 6050));
    assert(!secret.Input(L'D', 6100));
    assert(!secret.Input(L'I', 6150));
    assert(!secret.Input(L'S', 6200));
    assert(!secret.Input(L'C', 6250));
    assert(secret.Input(L'O', 6300));

    assert(!secret.Input(L'D', 6500));
    assert(!secret.Input(L'X', 6600));
    assert(!secret.Input(L'I', 6700));
    assert(!secret.Input(L'S', 6800));
    assert(!secret.Input(L'C', 6900));
    assert(!secret.Input(L'O', 7000));
}
