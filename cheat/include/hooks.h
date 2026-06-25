#pragma once

class Hooks {
public:
    Hooks() = default;
    ~Hooks() = default;

    bool Initialize() { return true; }
    void Shutdown() {}
};
