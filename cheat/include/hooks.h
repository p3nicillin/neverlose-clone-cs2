#pragma once

class Hooks {
public:
    Hooks() : m_initialized(false) {}
    ~Hooks() = default;

    bool Initialize() {
        m_initialized = true;
        return true;
    }
    void Shutdown() { m_initialized = false; }
    bool IsInitialized() const { return m_initialized; }

private:
    bool m_initialized;
};
