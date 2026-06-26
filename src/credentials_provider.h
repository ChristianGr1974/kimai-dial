#pragma once

#include <Arduino.h>

// Thin interface that decouples KimaiClient from SettingsStore (see
// settings_store.h: SettingsCredentialsProvider implements this by
// internally calling the existing SettingsStore functions). Passed by
// reference - no heap allocation, no DI container.
class ICredentialsProvider {
public:
    virtual ~ICredentialsProvider() = default;
    virtual String kimaiBaseUrl() const = 0;
    virtual String kimaiToken() const = 0;
};
