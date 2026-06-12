#ifndef MOCK_REGISTRY_H
#define MOCK_REGISTRY_H

#include "../../logos_registry.h"

/**
 * @brief Trivial LogosRegistry implementation for mock mode.
 *
 * No real registry endpoint is needed in mock mode; this implementation
 * simply reports itself as initialized so that callers do not stall.
 */
class MockRegistry : public LogosRegistry {
public:
    bool isInitialized() const override { return true; }
};

#endif // MOCK_REGISTRY_H
