#pragma once

#include "value.h"

#include <utility>

struct ReturnSignal {
    explicit ReturnSignal(Value value) : value(std::move(value)) {}

    Value value;
};